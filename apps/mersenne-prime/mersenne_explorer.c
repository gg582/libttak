#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

#define STATE_DIR "/home/yjlee/Documents"
#define STATE_FILE STATE_DIR "/found_mersenne.json"
#define STATE_FILE_TMP STATE_DIR "/found_mersenne.json.tmp"

#define MAX_ID_LEN 64
#define TASK_QUEUE_CAPACITY 128
#define RESULT_QUEUE_CAPACITY 128
#define DEFAULT_WORKERS 4
#define MAX_WORKERS 16
#define PERSIST_BATCH 4
#define PERSIST_INTERVAL_NS 500000000ULL
#define CANCEL_CHECK_MASK 0xFFu

static void ensure_state_dir(void) {
    struct stat st;
    if (stat(STATE_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return;
    }
    if (mkdir(STATE_DIR, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to ensure %s exists: %s\n", STATE_DIR, strerror(errno));
    }
}

typedef enum {
    TASK_STATE_CREATED = 0,
    TASK_STATE_STARTED,
    TASK_STATE_CANCELLED,
    TASK_STATE_FINISHED_COMPOSITE,
    TASK_STATE_FINISHED_PRIME,
    TASK_STATE_ERROR
} task_state_t;

typedef enum {
    TASK_ERROR_NONE = 0,
    TASK_ERROR_CANCELLED,
    TASK_ERROR_LUCAS_LEHMER,
    TASK_ERROR_INTERNAL
} task_error_t;

typedef enum {
    LL_STATUS_ERROR = -1,
    LL_STATUS_COMPOSITE = 0,
    LL_STATUS_PRIME = 1,
    LL_STATUS_CANCELLED = 2
} ll_status_t;

typedef struct {
    uint32_t p;
    task_state_t state;
    uint32_t iterations_done;
    uint64_t elapsed_ms;
    bool residue_is_zero;
    task_error_t error_code;
} mersenne_task_t;

typedef struct {
    void **items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    bool closed;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} queue_t;

typedef struct {
    uint64_t *words;
    size_t len;
    size_t cap;
} big_uint_t;

typedef struct {
    uint32_t p;
    size_t bits;
    size_t word_count;
    uint64_t last_mask;
    big_uint_t modulus;
} mersenne_mod_t;

typedef struct {
    uint32_t p;
    bool is_prime;
    uint32_t iterations;
    uint64_t elapsed_ms;
    task_state_t state;
} persisted_entry_t;

typedef struct {
    persisted_entry_t *entries;
    size_t count;
    size_t capacity;
    size_t dirty;
    uint32_t last_p_started;
    uint32_t last_p_finished;
    uint64_t last_flush_ns;
    char computer_id[MAX_ID_LEN];
    char user_id[MAX_ID_LEN];
} persistence_ctx_t;

typedef struct {
    queue_t *task_queue;
    queue_t *result_queue;
    const atomic_bool *shutdown_flag;
} worker_ctx_t;

typedef struct {
    queue_t *task_queue;
    const atomic_bool *shutdown_flag;
} producer_ctx_t;

typedef struct {
    queue_t *result_queue;
    persistence_ctx_t *persistence;
    const atomic_bool *shutdown_flag;
    const atomic_uint *last_started_ref;
} logger_ctx_t;

static queue_t g_task_queue;
static queue_t g_result_queue;
static atomic_bool g_shutdown = ATOMIC_VAR_INIT(false);
static atomic_uint g_next_candidate = ATOMIC_VAR_INIT(3);
static atomic_uint g_last_p_started = ATOMIC_VAR_INIT(3);

static uint64_t time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t ns_to_ms(uint64_t ns) {
    return ns / 1000000ULL;
}

static bool queue_init(queue_t *q, size_t capacity) {
    if (!q || capacity == 0) return false;
    memset(q, 0, sizeof(*q));
    q->items = calloc(capacity, sizeof(void *));
    if (!q->items) return false;
    q->capacity = capacity;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return true;
}

static void queue_destroy(queue_t *q) {
    if (!q) return;
    free(q->items);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

static void queue_close(queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    q->closed = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

static void queue_wake_all(queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

static bool queue_push(queue_t *q, void *item, const atomic_bool *shutdown_flag) {
    pthread_mutex_lock(&q->mutex);
    while (!q->closed && q->count == q->capacity) {
        if (shutdown_flag && atomic_load_explicit(shutdown_flag, memory_order_relaxed)) {
            pthread_mutex_unlock(&q->mutex);
            return false;
        }
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    if (q->closed) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }
    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

static void* queue_pop(queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0 && !q->closed) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    if (q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }
    void *item = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return item;
}

static void big_init(big_uint_t *n) {
    n->words = NULL;
    n->len = 0;
    n->cap = 0;
}

static void big_free(big_uint_t *n) {
    if (!n) return;
    free(n->words);
    n->words = NULL;
    n->len = 0;
    n->cap = 0;
}

static bool big_reserve(big_uint_t *n, size_t needed) {
    if (needed <= n->cap) return true;
    size_t new_cap = n->cap ? n->cap : 4;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    uint64_t *tmp = realloc(n->words, new_cap * sizeof(uint64_t));
    if (!tmp) return false;
    n->words = tmp;
    n->cap = new_cap;
    return true;
}

static void big_trim(big_uint_t *n) {
    while (n->len > 0 && n->words[n->len - 1] == 0) {
        n->len--;
    }
}

static void big_set_zero(big_uint_t *n) {
    n->len = 0;
}

static bool big_set_u64(big_uint_t *n, uint64_t value) {
    if (value == 0) {
        big_set_zero(n);
        return true;
    }
    if (!big_reserve(n, 1)) return false;
    n->words[0] = value;
    n->len = 1;
    return true;
}

static bool big_copy(big_uint_t *dst, const big_uint_t *src) {
    if (!big_reserve(dst, src->len ? src->len : 1)) return false;
    if (src->len > 0) {
        memcpy(dst->words, src->words, src->len * sizeof(uint64_t));
    }
    dst->len = src->len;
    return true;
}

static bool big_add_assign(big_uint_t *dst, const big_uint_t *src) {
    size_t max_len = dst->len > src->len ? dst->len : src->len;
    if (!big_reserve(dst, max_len + 1)) return false;
    uint64_t carry = 0;
    for (size_t i = 0; i < max_len; ++i) {
        uint64_t a = (i < dst->len) ? dst->words[i] : 0;
        uint64_t b = (i < src->len) ? src->words[i] : 0;
        unsigned __int128 sum = (unsigned __int128)a + b + carry;
        dst->words[i] = (uint64_t)sum;
        carry = (uint64_t)(sum >> 64);
    }
    dst->len = max_len;
    if (carry) {
        dst->words[max_len] = carry;
        dst->len = max_len + 1;
    }
    big_trim(dst);
    return true;
}

static bool big_sub_u64(big_uint_t *dst, uint64_t value) {
    if (dst->len == 0) return value == 0;
    unsigned __int128 cur = dst->words[0];
    unsigned __int128 sub = value;
    uint64_t borrow = 0;
    if (cur >= sub) {
        dst->words[0] = (uint64_t)(cur - sub);
    } else {
        dst->words[0] = (uint64_t)(((unsigned __int128)1 << 64) + cur - sub);
        borrow = 1;
    }
    for (size_t i = 1; i < dst->len && borrow; ++i) {
        if (dst->words[i] > 0) {
            dst->words[i]--;
            borrow = 0;
        } else {
            dst->words[i] = UINT64_MAX;
            borrow = 1;
        }
    }
    if (borrow) return false;
    big_trim(dst);
    return true;
}

static bool big_square(const big_uint_t *a, big_uint_t *out) {
    size_t n = a->len;
    if (n == 0) {
        big_set_zero(out);
        return true;
    }
    size_t out_len = n * 2;
    if (!big_reserve(out, out_len + 1)) return false;
    memset(out->words, 0, (out_len + 1) * sizeof(uint64_t));
    for (size_t i = 0; i < n; ++i) {
        unsigned __int128 carry = 0;
        for (size_t j = 0; j < n; ++j) {
            size_t idx = i + j;
            unsigned __int128 prod = (unsigned __int128)a->words[i] * (unsigned __int128)a->words[j];
            unsigned __int128 sum = (unsigned __int128)out->words[idx] + prod + carry;
            out->words[idx] = (uint64_t)sum;
            carry = sum >> 64;
        }
        size_t idx = i + n;
        while (carry) {
            unsigned __int128 sum = (unsigned __int128)out->words[idx] + carry;
            out->words[idx] = (uint64_t)sum;
            carry = sum >> 64;
            idx++;
        }
    }
    out->len = out_len + 1;
    big_trim(out);
    return true;
}

static bool big_shift_right(const big_uint_t *src, size_t bits, big_uint_t *dst) {
    if (bits == 0) {
        return big_copy(dst, src);
    }
    size_t word_shift = bits / 64;
    size_t bit_shift = bits % 64;
    if (word_shift >= src->len) {
        big_set_zero(dst);
        return true;
    }
    size_t new_len = src->len - word_shift;
    if (!big_reserve(dst, new_len)) return false;
    if (bit_shift == 0) {
        for (size_t i = 0; i < new_len; ++i) {
            dst->words[i] = src->words[i + word_shift];
        }
    } else {
        for (size_t i = 0; i < new_len; ++i) {
            uint64_t low = src->words[i + word_shift] >> bit_shift;
            uint64_t high = 0;
            if (i + word_shift + 1 < src->len) {
                high = src->words[i + word_shift + 1] << (64 - bit_shift);
            }
            dst->words[i] = low | high;
        }
    }
    dst->len = new_len;
    big_trim(dst);
    return true;
}

static bool big_is_zero(const big_uint_t *n) {
    return n->len == 0;
}

static bool mersenne_mod_init(mersenne_mod_t *mod, uint32_t p) {
    memset(mod, 0, sizeof(*mod));
    if (p < 2) return false;
    mod->p = p;
    mod->bits = p;
    mod->word_count = (p + 63u) / 64u;
    if (mod->word_count == 0) return false;
    uint32_t rem = p % 64u;
    mod->last_mask = rem == 0 ? UINT64_MAX : ((1ULL << rem) - 1ULL);
    big_init(&mod->modulus);
    if (!big_reserve(&mod->modulus, mod->word_count)) return false;
    for (size_t i = 0; i < mod->word_count - 1; ++i) {
        mod->modulus.words[i] = UINT64_MAX;
    }
    mod->modulus.words[mod->word_count - 1] = mod->last_mask;
    mod->modulus.len = mod->word_count;
    return true;
}

static void mersenne_mod_free(mersenne_mod_t *mod) {
    big_free(&mod->modulus);
}

static void mersenne_apply_mask(big_uint_t *value, const mersenne_mod_t *mod) {
    if (value->len > mod->word_count) {
        value->len = mod->word_count;
    }
    if (value->len == mod->word_count && mod->last_mask != UINT64_MAX) {
        value->words[mod->word_count - 1] &= mod->last_mask;
    }
    big_trim(value);
}

static bool big_equals_modulus(const big_uint_t *value, const mersenne_mod_t *mod) {
    if (value->len == 0) return false;
    if (value->len != mod->word_count) return false;
    for (size_t i = 0; i < mod->word_count - 1; ++i) {
        if (value->words[i] != UINT64_MAX) return false;
    }
    if (value->words[mod->word_count - 1] != mod->last_mask) return false;
    return true;
}

static bool mersenne_reduce(big_uint_t *value, const mersenne_mod_t *mod, big_uint_t *scratch) {
    while (value->len > mod->word_count ||
           (value->len == mod->word_count && mod->last_mask != UINT64_MAX &&
            (value->words[mod->word_count - 1] & ~mod->last_mask))) {
        if (!big_shift_right(value, mod->bits, scratch)) return false;
        mersenne_apply_mask(value, mod);
        if (!big_add_assign(value, scratch)) return false;
    }
    mersenne_apply_mask(value, mod);
    if (big_equals_modulus(value, mod)) {
        big_set_zero(value);
    }
    return true;
}

static uint64_t modmul_u64(uint64_t a, uint64_t b, uint64_t mod) {
    return (uint64_t)(((__uint128_t)a * b) % mod);
}

static uint64_t modpow_u64(uint64_t base, uint64_t exp, uint64_t mod) {
    uint64_t result = 1;
    uint64_t cur = base % mod;
    while (exp) {
        if (exp & 1ULL) {
            result = modmul_u64(result, cur, mod);
        }
        cur = modmul_u64(cur, cur, mod);
        exp >>= 1ULL;
    }
    return result;
}

static bool is_prime32(uint32_t n) {
    if (n < 2) return false;
    static const uint32_t small_primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31};
    for (size_t i = 0; i < sizeof(small_primes) / sizeof(small_primes[0]); ++i) {
        uint32_t p = small_primes[i];
        if (n == p) return true;
        if (n % p == 0) return false;
    }
    uint32_t d = n - 1;
    unsigned int r = 0;
    while ((d & 1u) == 0) {
        d >>= 1u;
        r++;
    }
    const uint32_t bases[] = {2, 3, 5, 7, 11};
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        uint64_t a = bases[i];
        if (a % n == 0) continue;
        uint64_t x = modpow_u64(a, d, n);
        if (x == 1 || x == n - 1) continue;
        bool composite = true;
        for (unsigned int j = 1; j < r; ++j) {
            x = modmul_u64(x, x, n);
            if (x == n - 1) {
                composite = false;
                break;
            }
        }
        if (composite) return false;
    }
    return true;
}

static inline uint32_t expected_iterations(uint32_t p) {
    return p <= 2 ? 0 : (p - 2);
}

static ll_status_t lucas_lehmer_run(uint32_t p, const atomic_bool *shutdown_flag, mersenne_task_t *task) {
    if (p < 2) {
        return LL_STATUS_ERROR;
    }
    if (p == 2) {
        task->iterations_done = 0;
        task->residue_is_zero = true;
        return LL_STATUS_PRIME;
    }
    mersenne_mod_t mod;
    if (!mersenne_mod_init(&mod, p)) {
        return LL_STATUS_ERROR;
    }
    big_uint_t s, square, scratch;
    big_init(&s);
    big_init(&square);
    big_init(&scratch);
    ll_status_t status = LL_STATUS_ERROR;
    if (!big_set_u64(&s, 4)) goto cleanup;
    uint32_t iterations = p - 2;
    for (uint32_t iter = 0; iter < iterations; ++iter) {
        if ((iter & CANCEL_CHECK_MASK) == 0 && shutdown_flag &&
            atomic_load_explicit(shutdown_flag, memory_order_relaxed)) {
            task->iterations_done = iter;
            status = LL_STATUS_CANCELLED;
            goto cleanup;
        }
        if (!big_square(&s, &square)) goto cleanup;
        if (!mersenne_reduce(&square, &mod, &scratch)) goto cleanup;
        if (!big_sub_u64(&square, 2)) {
            if (!big_add_assign(&square, &mod.modulus)) goto cleanup;
            if (!big_sub_u64(&square, 2)) goto cleanup;
        }
        if (!big_copy(&s, &square)) goto cleanup;
    }
    task->iterations_done = iterations;
    task->residue_is_zero = big_is_zero(&s);
    status = task->residue_is_zero ? LL_STATUS_PRIME : LL_STATUS_COMPOSITE;

cleanup:
    big_free(&scratch);
    big_free(&square);
    big_free(&s);
    mersenne_mod_free(&mod);
    return status;
}

static const char* state_to_string(task_state_t state) {
    switch (state) {
        case TASK_STATE_FINISHED_PRIME: return "PRIME";
        case TASK_STATE_FINISHED_COMPOSITE: return "COMPOSITE";
        case TASK_STATE_CANCELLED: return "CANCELLED";
        case TASK_STATE_ERROR: return "ERROR";
        case TASK_STATE_STARTED: return "STARTED";
        default: return "CREATED";
    }
}

static task_state_t state_from_string(const char *s) {
    if (!s) return TASK_STATE_ERROR;
    if (strcmp(s, "PRIME") == 0) return TASK_STATE_FINISHED_PRIME;
    if (strcmp(s, "COMPOSITE") == 0) return TASK_STATE_FINISHED_COMPOSITE;
    if (strcmp(s, "CANCELLED") == 0) return TASK_STATE_CANCELLED;
    if (strcmp(s, "ERROR") == 0) return TASK_STATE_ERROR;
    if (strcmp(s, "STARTED") == 0) return TASK_STATE_STARTED;
    return TASK_STATE_CREATED;
}

static void persistence_init(persistence_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->last_flush_ns = time_now_ns();
}

static void persistence_free(persistence_ctx_t *ctx) {
    free(ctx->entries);
    ctx->entries = NULL;
    ctx->count = ctx->capacity = ctx->dirty = 0;
}

static bool persistence_reserve(persistence_ctx_t *ctx, size_t needed) {
    if (needed <= ctx->capacity) return true;
    size_t new_cap = ctx->capacity ? ctx->capacity * 2 : 32;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    persisted_entry_t *tmp = realloc(ctx->entries, new_cap * sizeof(persisted_entry_t));
    if (!tmp) return false;
    ctx->entries = tmp;
    ctx->capacity = new_cap;
    return true;
}

static bool persistence_append(persistence_ctx_t *ctx, const mersenne_task_t *task) {
    if (!persistence_reserve(ctx, ctx->count + 1)) return false;
    persisted_entry_t *entry = &ctx->entries[ctx->count++];
    entry->p = task->p;
    entry->is_prime = (task->state == TASK_STATE_FINISHED_PRIME);
    entry->iterations = task->iterations_done;
    entry->elapsed_ms = task->elapsed_ms;
    entry->state = task->state;
    if ((task->state == TASK_STATE_FINISHED_COMPOSITE || task->state == TASK_STATE_FINISHED_PRIME) &&
        task->p > ctx->last_p_finished) {
        ctx->last_p_finished = task->p;
    }
    ctx->dirty++;
    return true;
}

static const char* json_key(const char *key, char *buf, size_t buf_len) {
    snprintf(buf, buf_len, "\"%s\"", key);
    return buf;
}

static const char* skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static bool json_read_uint32(const char *json, const char *key, uint32_t *out) {
    char keybuf[64];
    const char *pattern = json_key(key, keybuf, sizeof(keybuf));
    const char *pos = strstr(json, pattern);
    if (!pos) return false;
    pos = strchr(pos, ':');
    if (!pos) return false;
    pos = skip_ws(pos + 1);
    char *endptr = NULL;
    unsigned long val = strtoul(pos, &endptr, 10);
    if (pos == endptr) return false;
    *out = (uint32_t)val;
    return true;
}

static bool json_read_uint64(const char *json, const char *key, uint64_t *out) {
    char keybuf[64];
    const char *pattern = json_key(key, keybuf, sizeof(keybuf));
    const char *pos = strstr(json, pattern);
    if (!pos) return false;
    pos = strchr(pos, ':');
    if (!pos) return false;
    pos = skip_ws(pos + 1);
    char *endptr = NULL;
    unsigned long long val = strtoull(pos, &endptr, 10);
    if (pos == endptr) return false;
    *out = (uint64_t)val;
    return true;
}

static bool json_read_bool(const char *json, const char *key, bool *out) {
    char keybuf[64];
    const char *pattern = json_key(key, keybuf, sizeof(keybuf));
    const char *pos = strstr(json, pattern);
    if (!pos) return false;
    pos = strchr(pos, ':');
    if (!pos) return false;
    pos = skip_ws(pos + 1);
    if (strncmp(pos, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool json_read_string(const char *json, const char *key, char *out, size_t out_len) {
    char keybuf[64];
    const char *pattern = json_key(key, keybuf, sizeof(keybuf));
    const char *pos = strstr(json, pattern);
    if (!pos) return false;
    pos = strchr(pos, ':');
    if (!pos) return false;
    pos = strchr(pos, '"');
    if (!pos) {
        out[0] = '\0';
        return false;
    }
    pos++;
    size_t i = 0;
    while (*pos && *pos != '"' && i + 1 < out_len) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return true;
}

static bool persistence_load(persistence_ctx_t *ctx, const char *path) {
    ensure_state_dir();
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return false;
    }
    rewind(f);
    char *buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(f);
        return false;
    }
    size_t read = fread(buffer, 1, (size_t)size, f);
    buffer[read] = '\0';
    fclose(f);

    json_read_string(buffer, "computerid", ctx->computer_id, sizeof(ctx->computer_id));
    json_read_string(buffer, "userid", ctx->user_id, sizeof(ctx->user_id));
    json_read_uint32(buffer, "last_p_started", &ctx->last_p_started);
    json_read_uint32(buffer, "last_p_finished", &ctx->last_p_finished);

    char *results_section = strstr(buffer, "\"results\"");
    if (results_section) {
        char *array_start = strchr(results_section, '[');
        char *array_end = strchr(results_section, ']');
        if (array_start && array_end && array_end > array_start) {
            char *cursor = array_start;
            while (cursor < array_end) {
                char *obj_start = strchr(cursor, '{');
                if (!obj_start || obj_start >= array_end) break;
                char *obj_end = strchr(obj_start, '}');
                if (!obj_end || obj_end > array_end) break;
                size_t len = (size_t)(obj_end - obj_start + 1);
                char *entry = malloc(len + 1);
                if (!entry) break;
                memcpy(entry, obj_start, len);
                entry[len] = '\0';
                persisted_entry_t rec = {0};
                uint32_t status_iterations = 0;
                uint64_t elapsed = 0;
                bool is_prime = false;
                char status_str[32] = {0};
                if (json_read_uint32(entry, "p", &rec.p) &&
                    json_read_bool(entry, "is_prime", &is_prime) &&
                    json_read_uint32(entry, "iterations", &status_iterations) &&
                    json_read_uint64(entry, "elapsed_ms", &elapsed) &&
                    json_read_string(entry, "status", status_str, sizeof(status_str))) {
                    rec.is_prime = is_prime;
                    rec.iterations = status_iterations;
                    rec.elapsed_ms = elapsed;
                    rec.state = state_from_string(status_str);
                    if (persistence_reserve(ctx, ctx->count + 1)) {
                        ctx->entries[ctx->count++] = rec;
                        if ((rec.state == TASK_STATE_FINISHED_COMPOSITE ||
                             rec.state == TASK_STATE_FINISHED_PRIME) &&
                            rec.p > ctx->last_p_finished) {
                            ctx->last_p_finished = rec.p;
                        }
                    }
                }
                free(entry);
                cursor = obj_end + 1;
            }
        }
    }
    free(buffer);
    ctx->dirty = 0;
    return true;
}

static bool persistence_flush(persistence_ctx_t *ctx, const char *path, uint32_t started_value) {
    ensure_state_dir();
    FILE *tmp = fopen(STATE_FILE_TMP, "w");
    if (!tmp) {
        fprintf(stderr, "Failed to open temp state file: %s\n", strerror(errno));
        return false;
    }
    ctx->last_p_started = started_value;
    fprintf(tmp, "{\n");
    fprintf(tmp, "  \"computerid\": \"%s\",\n", ctx->computer_id);
    fprintf(tmp, "  \"userid\": \"%s\",\n", ctx->user_id);
    fprintf(tmp, "  \"last_p_started\": %u,\n", ctx->last_p_started);
    fprintf(tmp, "  \"last_p_finished\": %u,\n", ctx->last_p_finished);
    fprintf(tmp, "  \"results\": [\n");
    for (size_t i = 0; i < ctx->count; ++i) {
        const persisted_entry_t *entry = &ctx->entries[i];
        fprintf(tmp,
                "    { \"p\": %u, \"is_prime\": %s, \"iterations\": %u, \"elapsed_ms\": %" PRIu64
                ", \"status\": \"%s\" }%s\n",
                entry->p, entry->is_prime ? "true" : "false", entry->iterations, entry->elapsed_ms,
                state_to_string(entry->state), (i + 1 == ctx->count) ? "" : ",");
    }
    fprintf(tmp, "  ]\n}\n");

    if (fflush(tmp) != 0) {
        fprintf(stderr, "fflush failed for state file: %s\n", strerror(errno));
        fclose(tmp);
        unlink(STATE_FILE_TMP);
        return false;
    }
    int fd = fileno(tmp);
    if (fd >= 0 && fsync(fd) != 0) {
        fprintf(stderr, "fsync failed for state file: %s\n", strerror(errno));
        fclose(tmp);
        unlink(STATE_FILE_TMP);
        return false;
    }
    fclose(tmp);
    if (rename(STATE_FILE_TMP, path) != 0) {
        fprintf(stderr, "rename failed for state file: %s\n", strerror(errno));
        unlink(STATE_FILE_TMP);
        return false;
    }
    ctx->dirty = 0;
    ctx->last_flush_ns = time_now_ns();
    return true;
}

static bool persistence_maybe_flush(persistence_ctx_t *ctx, uint32_t started_value, bool force) {
    if (!force) {
        uint64_t now = time_now_ns();
        if (ctx->dirty < PERSIST_BATCH && (now - ctx->last_flush_ns) < PERSIST_INTERVAL_NS) {
            return true;
        }
    }
    if (ctx->dirty == 0 && !force) {
        return true;
    }
    return persistence_flush(ctx, STATE_FILE, started_value);
}

static void handle_sigint(int sig) {
    (void)sig;
    atomic_store_explicit(&g_shutdown, true, memory_order_relaxed);
    queue_wake_all(&g_task_queue);
    queue_wake_all(&g_result_queue);
}

static void free_task(mersenne_task_t *task) {
    free(task);
}

static void *producer_thread(void *arg) {
    producer_ctx_t *ctx = (producer_ctx_t *)arg;
    uint32_t p = atomic_load_explicit(&g_next_candidate, memory_order_relaxed);
    if ((p & 1u) == 0) p++;
    while (!atomic_load_explicit(ctx->shutdown_flag, memory_order_relaxed)) {
        if (!is_prime32(p)) {
            if (p >= UINT32_MAX - 2u) break;
            p += 2u;
            continue;
        }
        mersenne_task_t *task = calloc(1, sizeof(*task));
        if (!task) {
            fprintf(stderr, "Out of memory allocating task\n");
            atomic_store_explicit(&g_shutdown, true, memory_order_relaxed);
            queue_wake_all(&g_task_queue);
            break;
        }
        task->p = p;
        task->state = TASK_STATE_CREATED;
        if (!queue_push(ctx->task_queue, task, ctx->shutdown_flag)) {
            free_task(task);
            break;
        }
        atomic_store_explicit(&g_last_p_started, p, memory_order_relaxed);
        if (p >= UINT32_MAX - 2u) break;
        p += 2u;
    }
    return NULL;
}

static void *worker_thread(void *arg) {
    worker_ctx_t *ctx = (worker_ctx_t *)arg;
    while (true) {
        mersenne_task_t *task = queue_pop(ctx->task_queue);
        if (!task) break;
        task->state = TASK_STATE_STARTED;
        task->error_code = TASK_ERROR_NONE;
        task->residue_is_zero = false;
        uint64_t start_ns = time_now_ns();
        ll_status_t status = lucas_lehmer_run(task->p, ctx->shutdown_flag, task);
        uint64_t end_ns = time_now_ns();
        task->elapsed_ms = ns_to_ms(end_ns - start_ns);
        switch (status) {
            case LL_STATUS_PRIME:
                task->state = TASK_STATE_FINISHED_PRIME;
                task->error_code = TASK_ERROR_NONE;
                break;
            case LL_STATUS_COMPOSITE:
                task->state = TASK_STATE_FINISHED_COMPOSITE;
                task->error_code = TASK_ERROR_NONE;
                break;
            case LL_STATUS_CANCELLED:
                task->state = TASK_STATE_CANCELLED;
                task->error_code = TASK_ERROR_CANCELLED;
                break;
            default:
                task->state = TASK_STATE_ERROR;
                task->error_code = TASK_ERROR_LUCAS_LEHMER;
                atomic_store_explicit(&g_shutdown, true, memory_order_relaxed);
                queue_wake_all(&g_task_queue);
                queue_wake_all(&g_result_queue);
                break;
        }
        if (!queue_push(ctx->result_queue, task, NULL)) {
            fprintf(stderr, "Result queue closed unexpectedly\n");
            free_task(task);
            break;
        }
    }
    return NULL;
}

static void log_result(logger_ctx_t *ctx, mersenne_task_t *task) {
    uint32_t expected = expected_iterations(task->p);
    bool deterministic = (task->iterations_done == expected);
    bool found = (task->state == TASK_STATE_FINISHED_PRIME) && task->residue_is_zero && deterministic;
    if (found) {
        printf("[FOUND] M%u is prime!\n", task->p);
        fflush(stdout);
    }
    if (!persistence_append(ctx->persistence, task)) {
        fprintf(stderr, "Failed to persist task result for p=%u\n", task->p);
    } else {
        persistence_maybe_flush(ctx->persistence,
                                atomic_load_explicit(ctx->last_started_ref, memory_order_relaxed),
                                false);
    }
}

static void *logger_thread(void *arg) {
    logger_ctx_t *ctx = (logger_ctx_t *)arg;
    while (true) {
        mersenne_task_t *task = queue_pop(ctx->result_queue);
        if (!task) break;
        log_result(ctx, task);
        free_task(task);
    }
    persistence_maybe_flush(ctx->persistence,
                            atomic_load_explicit(ctx->last_started_ref, memory_order_relaxed),
                            true);
    return NULL;
}

static uint32_t compute_resume_p(const persistence_ctx_t *persistence) {
    uint32_t base = 3;
    if (persistence->last_p_finished >= 3) {
        base = persistence->last_p_finished + 2;
    }
    if (persistence->last_p_started > persistence->last_p_finished) {
        base = persistence->last_p_started;
    }
    if ((base & 1u) == 0) base++;
    return base;
}

#ifdef TTAK_SELFTEST
static void run_selftest(void) {
    const uint32_t known_primes[] = {2, 3, 5, 7, 13, 17, 19, 31, 61, 89, 107, 127};
    const uint32_t non_primes[] = {11, 23, 29};
    for (size_t i = 0; i < sizeof(known_primes) / sizeof(known_primes[0]); ++i) {
        mersenne_task_t task = {.p = known_primes[i]};
        ll_status_t status = lucas_lehmer_run(task.p, NULL, &task);
        if (status != LL_STATUS_PRIME) {
            fprintf(stderr, "[selftest] Expected prime for p=%u\n", task.p);
            exit(EXIT_FAILURE);
        }
    }
    for (size_t i = 0; i < sizeof(non_primes) / sizeof(non_primes[0]); ++i) {
        mersenne_task_t task = {.p = non_primes[i]};
        ll_status_t status = lucas_lehmer_run(task.p, NULL, &task);
        if (status != LL_STATUS_COMPOSITE) {
            fprintf(stderr, "[selftest] Expected composite for p=%u\n", task.p);
            exit(EXIT_FAILURE);
        }
    }
    printf("[selftest] Lucas-Lehmer test cases passed\n");
}
#endif

int main(int argc, char **argv) {
    int worker_count = DEFAULT_WORKERS;
    if (argc > 1) {
        worker_count = atoi(argv[1]);
        if (worker_count <= 0) worker_count = DEFAULT_WORKERS;
        if (worker_count > MAX_WORKERS) worker_count = MAX_WORKERS;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    persistence_ctx_t persistence;
    persistence_init(&persistence);
    persistence_load(&persistence, STATE_FILE);
    uint32_t resume_p = compute_resume_p(&persistence);
    atomic_store_explicit(&g_next_candidate, resume_p, memory_order_relaxed);
    atomic_store_explicit(&g_last_p_started, persistence.last_p_started ? persistence.last_p_started : resume_p,
                          memory_order_relaxed);

#ifdef TTAK_SELFTEST
    run_selftest();
#endif

    if (!queue_init(&g_task_queue, TASK_QUEUE_CAPACITY)) {
        fprintf(stderr, "Failed to init task queue\n");
        return EXIT_FAILURE;
    }
    if (!queue_init(&g_result_queue, RESULT_QUEUE_CAPACITY)) {
        fprintf(stderr, "Failed to init result queue\n");
        queue_destroy(&g_task_queue);
        return EXIT_FAILURE;
    }

    producer_ctx_t producer_ctx = {.task_queue = &g_task_queue, .shutdown_flag = &g_shutdown};
    pthread_t producer_thread_id;
    if (pthread_create(&producer_thread_id, NULL, producer_thread, &producer_ctx) != 0) {
        fprintf(stderr, "Failed to start producer thread\n");
        queue_destroy(&g_task_queue);
        queue_destroy(&g_result_queue);
        return EXIT_FAILURE;
    }

    worker_ctx_t worker_ctx[MAX_WORKERS];
    pthread_t worker_threads[MAX_WORKERS];
    for (int i = 0; i < worker_count; ++i) {
        worker_ctx[i].task_queue = &g_task_queue;
        worker_ctx[i].result_queue = &g_result_queue;
        worker_ctx[i].shutdown_flag = &g_shutdown;
        if (pthread_create(&worker_threads[i], NULL, worker_thread, &worker_ctx[i]) != 0) {
            fprintf(stderr, "Failed to start worker thread %d\n", i);
            atomic_store_explicit(&g_shutdown, true, memory_order_relaxed);
            queue_wake_all(&g_task_queue);
            queue_wake_all(&g_result_queue);
            worker_count = i;
            break;
        }
    }

    logger_ctx_t logger_ctx = {
        .result_queue = &g_result_queue,
        .persistence = &persistence,
        .shutdown_flag = &g_shutdown,
        .last_started_ref = &g_last_p_started,
    };
    pthread_t logger_thread_id;
    if (pthread_create(&logger_thread_id, NULL, logger_thread, &logger_ctx) != 0) {
        fprintf(stderr, "Failed to start logger thread\n");
        atomic_store_explicit(&g_shutdown, true, memory_order_relaxed);
        queue_wake_all(&g_task_queue);
        queue_wake_all(&g_result_queue);
        pthread_join(producer_thread_id, NULL);
        for (int i = 0; i < worker_count; ++i) {
            pthread_join(worker_threads[i], NULL);
        }
        queue_destroy(&g_task_queue);
        queue_destroy(&g_result_queue);
        persistence_free(&persistence);
        return EXIT_FAILURE;
    }

    while (!atomic_load_explicit(&g_shutdown, memory_order_relaxed)) {
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 200000000};
        nanosleep(&ts, NULL);
    }

    pthread_join(producer_thread_id, NULL);
    queue_close(&g_task_queue);
    for (int i = 0; i < worker_count; ++i) {
        pthread_join(worker_threads[i], NULL);
    }
    queue_close(&g_result_queue);
    pthread_join(logger_thread_id, NULL);

    persistence_maybe_flush(&persistence,
                            atomic_load_explicit(&g_last_p_started, memory_order_relaxed),
                            true);

    queue_destroy(&g_task_queue);
    queue_destroy(&g_result_queue);
    persistence_free(&persistence);
    return 0;
}
