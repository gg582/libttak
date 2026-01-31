#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <stdalign.h>
#include <limits.h>

#include <ttak/mem/mem.h>
#include <ttak/sync/sync.h>
#include <ttak/timing/timing.h>
#include <ttak/atomic/atomic.h>
#include <ttak/math/bigint.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define STATE_ENV_VAR       "ALIQUOT_STATE_DIR"
#define DEFAULT_STATE_DIR   "/opt/aliquot-tracker"
#define FOUND_LOG_NAME      "aliquot_found.jsonl"
#define JUMP_LOG_NAME       "aliquot_jump.jsonl"
#define TRACK_LOG_NAME      "aliquot_track.jsonl"
#define CATALOG_FILTER_FILE "catalog_filters.txt"
#define QUEUE_STATE_NAME    "aliquot_queue.json"

#define MAX_WORKERS         8
#define JOB_QUEUE_CAP       512
#define HISTORY_BUCKETS     4096
#define LONG_RUN_MAX_STEPS  25000
#define SCOUT_PREVIEW_STEPS 256
#define FLUSH_INTERVAL_MS   4000
#define SCOUT_SLEEP_MS      200
#define SCOUT_MIN_SEED      1000ULL
#define SCOUT_MAX_SEED      50000000ULL
#define SCOUT_SCORE_GATE    120.0
#define SCAN_STEP_CAP       64
#define SCAN_TIMECAP_MS     25
#define TRACK_PREFIX_DIGITS 48
#define TRACK_FAST_BUDGET_MS (30ULL * 60ULL * 1000ULL)
#define TRACK_DEEP_BUDGET_MS (2ULL * 60ULL * 60ULL * 1000ULL)
#define CATALOG_MAX_EXACT    512
#define CATALOG_MAX_MOD_RULE 256

typedef struct {
    uint64_t seed;
    uint64_t steps;
    uint64_t max_value;
    uint64_t final_value;
    uint32_t cycle_length;
    bool terminated;
    bool entered_cycle;
    bool amicable;
    bool perfect;
    bool overflow;
    bool hit_limit;
    bool time_budget_hit;
    bool catalog_hit;
    uint64_t wall_time_ms;
} aliquot_outcome_t;

typedef struct {
    uint64_t seed;
    uint64_t steps;
    uint64_t max_value;
    uint64_t final_value;
    uint32_t cycle_length;
    char status[24];
    char provenance[16];
} found_record_t;

typedef struct {
    uint64_t seed;
    uint64_t preview_steps;
    uint64_t preview_max;
    double score;
    double overflow_pressure;
} jump_record_t;

typedef enum {
    SCAN_END_CATALOG = 0,
    SCAN_END_OVERFLOW,
    SCAN_END_TIMECAP
} scan_end_reason_t;

typedef struct {
    uint64_t seed;
    uint64_t steps;
    uint64_t max_u64;
    scan_end_reason_t ended_by;
} scan_result_t;

typedef struct {
    uint64_t modulus;
    uint64_t remainder;
} catalog_mod_rule_t;

typedef struct {
    uint64_t seed;
    uint64_t steps;
    uint64_t wall_time_ms;
    uint64_t budget_ms;
    uint64_t max_u64;
    uint32_t max_bits;
    uint32_t max_dec_digits;
    double scout_score;
    uint32_t priority;
    char ended[24];
    char ended_by[32];
    char max_hash[65];
    char max_prefix[TRACK_PREFIX_DIGITS + 1];
} track_record_t;

typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t buffer[64];
    size_t used;
} sha256_ctx_t;

typedef struct history_entry {
    uint64_t value;
    uint32_t step;
    struct history_entry *next;
} history_entry_t;

typedef struct {
    history_entry_t *buckets[HISTORY_BUCKETS];
} history_table_t;

typedef struct {
    uint64_t seed;
    char provenance[16];
    uint32_t priority;
    double scout_score;
    uint64_t preview_steps;
    uint64_t preview_max;
    bool preview_overflow;
} aliquot_job_t;

typedef struct {
    aliquot_job_t buffer[JOB_QUEUE_CAP];
    size_t head;
    size_t tail;
    size_t count;
    ttak_mutex_t lock;
    ttak_cond_t cond;
} job_queue_t;

typedef struct seed_entry {
    uint64_t seed;
    struct seed_entry *next;
} seed_entry_t;

#define SEED_REGISTRY_BUCKETS 4096

typedef struct {
    alignas(64) atomic_uint_fast64_t op_count;
    uint32_t id;
    pthread_t thread;
} worker_ctx_t;

static volatile atomic_bool shutdown_requested = false;
static atomic_uint_fast64_t g_rng_state = 88172645463393265ULL;
static uint64_t g_total_sequences = 0;
static uint64_t g_total_probes = 0;
static uint64_t g_last_persist_ms = 0;

static worker_ctx_t g_workers[MAX_WORKERS];
static int g_worker_count = 4;
static job_queue_t g_job_queue;
static seed_entry_t *g_seed_buckets[SEED_REGISTRY_BUCKETS];
static ttak_mutex_t g_seed_lock = PTHREAD_MUTEX_INITIALIZER;
static ttak_mutex_t g_disk_lock = PTHREAD_MUTEX_INITIALIZER;

static found_record_t *g_found_records = NULL;
static size_t g_found_count = 0;
static size_t g_found_cap = 0;

static jump_record_t *g_jump_records = NULL;
static size_t g_jump_count = 0;
static size_t g_jump_cap = 0;

static track_record_t *g_track_records = NULL;
static size_t g_track_count = 0;
static size_t g_track_cap = 0;

static char g_state_dir[PATH_MAX] = DEFAULT_STATE_DIR;
static char g_found_log_path[PATH_MAX] = {0};
static char g_jump_log_path[PATH_MAX] = {0};
static char g_track_log_path[PATH_MAX] = {0};
static char g_queue_state_path[PATH_MAX] = {0};

static uint64_t g_catalog_exact[CATALOG_MAX_EXACT];
static size_t g_catalog_exact_count = 0;
static catalog_mod_rule_t g_catalog_mod_rules[CATALOG_MAX_MOD_RULE];
static size_t g_catalog_mod_rule_count = 0;

static const uint32_t g_catalog_seeds[] = {
    276, 552, 564, 660, 966, 1080, 1128, 1320, 1476, 1596, 1776, 1830,
    1968, 1980, 2208, 2268, 2310, 2514, 2760, 3000, 3192, 3276, 3468,
    3570, 3660, 3774, 4140, 4308, 4788, 5256, 5628, 5880, 6600, 7950,
    8280, 8910, 9660, 10584, 11088, 11844, 13068, 13260, 14880, 17640,
    18540, 18954, 21120, 21660, 23040, 27600, 31944, 35670, 38940,
    41256, 43032, 48828, 50976, 51480, 55440, 59280, 60120, 63504,
    66000, 72600, 79560, 83736, 90600, 100128, 111546, 114960,
    117936, 118260, 123660
};

static void handle_signal(int sig);
static void responsive_sleep(uint32_t ms);
static void seed_rng(void);
static void configure_state_paths(void);
static uint64_t monotonic_millis(void);
static uint64_t next_random64(void);
static uint64_t random_seed_between(uint64_t lo, uint64_t hi);
static void ensure_state_dir(void);
static uint64_t sum_proper_divisors(uint64_t n);
static void history_init(history_table_t *t);
static void history_destroy(history_table_t *t);
static bool history_contains(history_table_t *t, uint64_t value, uint32_t *step_out);
static void history_insert(history_table_t *t, uint64_t value, uint32_t step);
static bool seed_registry_try_add(uint64_t seed);
static void seed_registry_mark(uint64_t seed);
static const char *classify_outcome(const aliquot_outcome_t *out);
static void run_aliquot_sequence(uint64_t seed, uint32_t max_steps, uint64_t time_budget_ms, aliquot_outcome_t *out);
static double compute_probe_score(const aliquot_outcome_t *out);
static bool looks_long(const aliquot_outcome_t *out, double *score_out);
static double compute_overflow_pressure(const aliquot_outcome_t *out);
static bool frontier_accept_seed(uint64_t seed, scan_result_t *result);
static void job_queue_init(job_queue_t *q);
static bool job_queue_push(job_queue_t *q, const aliquot_job_t *job);
static bool job_queue_pop(job_queue_t *q, aliquot_job_t *job);
static size_t job_queue_count(job_queue_t *q);
static size_t job_queue_snapshot(job_queue_t *q, uint64_t *dest, size_t cap);
static bool ensure_found_capacity(size_t extra);
static bool ensure_jump_capacity(size_t extra);
static bool ensure_track_capacity(size_t extra);
static void append_found_record(const aliquot_outcome_t *out, const char *source);
static void append_jump_record(uint64_t seed, uint64_t steps, uint64_t max_value, double score, double overflow_pressure);
static void append_track_record(const aliquot_outcome_t *out, const aliquot_job_t *job, uint64_t budget_ms);
static void rehydrate_found_record(const found_record_t *rec);
static void rehydrate_jump_record(const jump_record_t *rec);
static void rehydrate_track_record(const track_record_t *rec);
static void persist_found_records(void);
static void persist_jump_records(void);
static void persist_track_records(void);
static void persist_queue_state(void);
static void flush_ledgers(void);
static void maybe_flush_ledgers(void);
static void load_found_records(void);
static void load_jump_records(void);
static void load_track_records(void);
static void load_queue_checkpoint(void);
static void init_catalog_filters(void);
static bool record_catalog_exact(uint64_t seed);
static bool record_catalog_mod(uint64_t modulus, uint64_t remainder);
static void load_catalog_filter_file(void);
static bool is_catalog_value(uint64_t value);
static uint64_t determine_time_budget(const aliquot_job_t *job);
static void capture_track_metrics(const aliquot_outcome_t *out, const aliquot_job_t *job, uint64_t budget_ms, track_record_t *rec);
static void format_track_end_detail(const aliquot_outcome_t *out, char *dest, size_t dest_cap);
static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[64]);
static void sha256_init(sha256_ctx_t *ctx);
static void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len);
static void sha256_final(sha256_ctx_t *ctx, uint8_t out[32]);
static void *worker_main(void *arg);
static void *scout_main(void *arg);
static void process_job(const aliquot_job_t *job);

static void handle_signal(int sig) {
    (void)sig;
    atomic_store_explicit(&shutdown_requested, true, memory_order_relaxed);
    ttak_mutex_lock(&g_job_queue.lock);
    ttak_cond_broadcast(&g_job_queue.cond);
    ttak_mutex_unlock(&g_job_queue.lock);
}

static uint64_t monotonic_millis(void) {
    return ttak_get_tick_count();
}

static void responsive_sleep(uint32_t ms) {
    const uint32_t chunk = 200;
    uint32_t waited = 0;
    while (waited < ms) {
        if (atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) break;
        uint32_t slice = ms - waited;
        if (slice > chunk) slice = chunk;
        struct timespec ts = {.tv_sec = slice / 1000, .tv_nsec = (slice % 1000) * 1000000L};
        nanosleep(&ts, NULL);
        waited += slice;
    }
}

static void configure_state_paths(void) {
    const char *override = getenv(STATE_ENV_VAR);
    const char *base = (override && *override) ? override : DEFAULT_STATE_DIR;
    snprintf(g_state_dir, sizeof(g_state_dir), "%s", base);
    g_state_dir[sizeof(g_state_dir) - 1] = '\0';
    size_t len = strlen(g_state_dir);
    while (len > 1 && g_state_dir[len - 1] == '/') {
        g_state_dir[--len] = '\0';
    }
    if (len == 0) {
        snprintf(g_state_dir, sizeof(g_state_dir), "%s", DEFAULT_STATE_DIR);
    }
    snprintf(g_found_log_path, sizeof(g_found_log_path), "%s/%s", g_state_dir, FOUND_LOG_NAME);
    snprintf(g_jump_log_path, sizeof(g_jump_log_path), "%s/%s", g_state_dir, JUMP_LOG_NAME);
    snprintf(g_track_log_path, sizeof(g_track_log_path), "%s/%s", g_state_dir, TRACK_LOG_NAME);
    snprintf(g_queue_state_path, sizeof(g_queue_state_path), "%s/%s", g_state_dir, QUEUE_STATE_NAME);
}

static void seed_rng(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t seed = ((uint64_t)ts.tv_nsec << 16) ^ (uint64_t)getpid();
    if (seed == 0) seed = 88172645463393265ULL;
    atomic_store(&g_rng_state, seed);
}

static uint64_t next_random64(void) {
    uint64_t x = atomic_load(&g_rng_state);
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    x *= 2685821657736338717ULL;
    atomic_store(&g_rng_state, x);
    return x;
}

static uint64_t random_seed_between(uint64_t lo, uint64_t hi) {
    if (hi <= lo) return lo;
    uint64_t span = hi - lo + 1ULL;
    return lo + (next_random64() % span);
}

static void ensure_state_dir(void) {
    struct stat st;
    if (stat(g_state_dir, &st) == 0 && S_ISDIR(st.st_mode)) return;
    if (mkdir(g_state_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[ALIQUOT] Failed to create %s: %s\n", g_state_dir, strerror(errno));
    }
}

static uint64_t sum_proper_divisors(uint64_t n) {
    if (n <= 1) return 0;
    uint64_t remaining = n;
    uint64_t sum = 1;
    for (uint64_t p = 2; p <= remaining / p; ++p) {
        if (remaining % p != 0) continue;
        unsigned __int128 power = 1;
        unsigned __int128 term = 1;
        do {
            remaining /= p;
            power *= p;
            term += power;
        } while (remaining % p == 0);
        unsigned __int128 new_sum = (unsigned __int128)sum * term;
        if (new_sum > UINT64_MAX) return UINT64_MAX;
        sum = (uint64_t)new_sum;
    }
    if (remaining > 1) {
        unsigned __int128 new_sum = (unsigned __int128)sum * (remaining + 1ULL);
        if (new_sum > UINT64_MAX) return UINT64_MAX;
        sum = (uint64_t)new_sum;
    }
    return (sum > n) ? (sum - n) : 0;
}

static void history_init(history_table_t *t) {
    memset(t, 0, sizeof(*t));
}

static void history_destroy(history_table_t *t) {
    for (size_t i = 0; i < HISTORY_BUCKETS; ++i) {
        history_entry_t *node = t->buckets[i];
        while (node) {
            history_entry_t *next = node->next;
            ttak_mem_free(node);
            node = next;
        }
        t->buckets[i] = NULL;
    }
}

static bool history_contains(history_table_t *t, uint64_t value, uint32_t *step_out) {
    size_t idx = value % HISTORY_BUCKETS;
    history_entry_t *node = t->buckets[idx];
    while (node) {
        if (node->value == value) {
            if (step_out) *step_out = node->step;
            return true;
        }
        node = node->next;
    }
    return false;
}

static void history_insert(history_table_t *t, uint64_t value, uint32_t step) {
    size_t idx = value % HISTORY_BUCKETS;
    uint64_t now = monotonic_millis();
    history_entry_t *node = ttak_mem_alloc(sizeof(*node), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) return;
    node->value = value;
    node->step = step;
    node->next = t->buckets[idx];
    t->buckets[idx] = node;
}

static bool seed_registry_try_add(uint64_t seed) {
    size_t idx = seed % SEED_REGISTRY_BUCKETS;
    ttak_mutex_lock(&g_seed_lock);
    seed_entry_t *node = g_seed_buckets[idx];
    while (node) {
        if (node->seed == seed) {
            ttak_mutex_unlock(&g_seed_lock);
            return false;
        }
        node = node->next;
    }
    uint64_t now = monotonic_millis();
    node = ttak_mem_alloc(sizeof(*node), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) {
        ttak_mutex_unlock(&g_seed_lock);
        return false;
    }
    node->seed = seed;
    node->next = g_seed_buckets[idx];
    g_seed_buckets[idx] = node;
    ttak_mutex_unlock(&g_seed_lock);
    return true;
}

static void seed_registry_mark(uint64_t seed) {
    (void)seed_registry_try_add(seed);
}

static bool record_catalog_exact(uint64_t seed) {
    for (size_t i = 0; i < g_catalog_exact_count; ++i) {
        if (g_catalog_exact[i] == seed) return true;
    }
    if (g_catalog_exact_count >= CATALOG_MAX_EXACT) return false;
    g_catalog_exact[g_catalog_exact_count++] = seed;
    return true;
}

static bool record_catalog_mod(uint64_t modulus, uint64_t remainder) {
    if (modulus == 0) return false;
    for (size_t i = 0; i < g_catalog_mod_rule_count; ++i) {
        if (g_catalog_mod_rules[i].modulus == modulus &&
            g_catalog_mod_rules[i].remainder == remainder) {
            return true;
        }
    }
    if (g_catalog_mod_rule_count >= CATALOG_MAX_MOD_RULE) return false;
    g_catalog_mod_rules[g_catalog_mod_rule_count++] = (catalog_mod_rule_t){
        .modulus = modulus,
        .remainder = remainder
    };
    return true;
}

static void load_catalog_filter_file(void) {
    char path[PATH_MAX + 64];
    snprintf(path, sizeof(path), "%s/%s", g_state_dir, CATALOG_FILTER_FILE);
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *ptr = line;
        while (isspace((unsigned char)*ptr)) ptr++;
        if (*ptr == '\0' || *ptr == '#') continue;
        uint64_t a = 0, b = 0;
        if (sscanf(ptr, "exact:%" SCNu64, &a) == 1 ||
            sscanf(ptr, "exact=%" SCNu64, &a) == 1) {
            record_catalog_exact(a);
            continue;
        }
        if (sscanf(ptr, "mod:%" SCNu64 ":%" SCNu64, &a, &b) == 2 ||
            sscanf(ptr, "mod=%" SCNu64 ":%" SCNu64, &a, &b) == 2) {
            record_catalog_mod(a, b % a);
            continue;
        }
    }
    fclose(fp);
}

static void init_catalog_filters(void) {
    g_catalog_exact_count = 0;
    g_catalog_mod_rule_count = 0;
    for (size_t i = 0; i < sizeof(g_catalog_seeds) / sizeof(g_catalog_seeds[0]); ++i) {
        record_catalog_exact(g_catalog_seeds[i]);
    }
    load_catalog_filter_file();
}

static bool is_catalog_value(uint64_t value) {
    for (size_t i = 0; i < g_catalog_exact_count; ++i) {
        if (g_catalog_exact[i] == value) return true;
    }
    for (size_t i = 0; i < g_catalog_mod_rule_count; ++i) {
        const catalog_mod_rule_t *rule = &g_catalog_mod_rules[i];
        if (rule->modulus && value % rule->modulus == rule->remainder) {
            return true;
        }
    }
    return false;
}

static const char *classify_outcome(const aliquot_outcome_t *out) {
    if (out->overflow) return "overflow";
    if (out->catalog_hit) return "catalog";
    if (out->perfect) return "perfect";
    if (out->amicable) return "amicable";
    if (out->terminated) return "terminated";
    if (out->entered_cycle) return "cycle";
    if (out->hit_limit) return "open-limit";
    return "open";
}

static bool frontier_accept_seed(uint64_t seed, scan_result_t *result) {
    if (result) memset(result, 0, sizeof(*result));
    if (result) result->seed = seed;
    if (is_catalog_value(seed)) {
        if (result) result->ended_by = SCAN_END_CATALOG;
        return false;
    }
    uint64_t start_ms = monotonic_millis();
    uint64_t current = seed;
    uint64_t max_value = seed;
    uint32_t steps = 0;
    while (steps < SCAN_STEP_CAP) {
        if (SCAN_TIMECAP_MS > 0) {
            uint64_t now = monotonic_millis();
            if (now - start_ms >= (uint64_t)SCAN_TIMECAP_MS) break;
        }
        uint64_t next = sum_proper_divisors(current);
        ttak_atomic_inc64(&g_total_probes);
        steps++;
        if (next > max_value) max_value = next;
        if (next == UINT64_MAX) {
            if (result) {
                result->ended_by = SCAN_END_OVERFLOW;
                result->steps = steps;
                result->max_u64 = max_value;
            }
            return true;
        }
        if (is_catalog_value(next)) {
            if (result) {
                result->ended_by = SCAN_END_CATALOG;
                result->steps = steps;
                result->max_u64 = max_value;
            }
            return false;
        }
        current = next;
    }
    if (result) {
        result->ended_by = SCAN_END_TIMECAP;
        result->steps = steps;
        result->max_u64 = max_value;
    }
    return true;
}

static void run_aliquot_sequence(uint64_t seed, uint32_t max_steps, uint64_t time_budget_ms, aliquot_outcome_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->seed = seed;
    out->max_value = seed;
    out->final_value = seed;
    uint64_t start_ms = monotonic_millis();

    history_table_t hist;
    history_init(&hist);
    history_insert(&hist, seed, 0);

    uint64_t current = seed;
    uint32_t steps = 0;
    while (true) {
        if (steps == 0 && is_catalog_value(current)) {
            out->catalog_hit = true;
            out->final_value = current;
            break;
        }
        if (max_steps > 0 && steps >= max_steps) {
            out->hit_limit = true;
            break;
        }
        if (time_budget_ms > 0) {
            uint64_t now = monotonic_millis();
            if (now - start_ms >= time_budget_ms) {
                out->hit_limit = true;
                out->time_budget_hit = true;
                break;
            }
        }
        if (atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) break;
        uint64_t next = sum_proper_divisors(current);
        ttak_atomic_inc64(&g_total_probes);
        if (next > out->max_value) out->max_value = next;
        steps++;
        if (next == UINT64_MAX) {
            out->overflow = true;
            out->final_value = UINT64_MAX;
            break;
        }
        if (next <= 1) {
            out->terminated = true;
            out->final_value = next;
            break;
        }
        uint32_t prev_step = 0;
        if (history_contains(&hist, next, &prev_step)) {
            out->entered_cycle = true;
            out->cycle_length = steps - prev_step;
            out->final_value = next;
            if (out->cycle_length <= 2) {
                if (out->cycle_length == 1 && next == seed) {
                    out->perfect = true;
                } else {
                    out->amicable = true;
                }
            }
            break;
        }
        if (is_catalog_value(next)) {
            out->catalog_hit = true;
            out->final_value = next;
            break;
        }
        history_insert(&hist, next, steps);
        current = next;
    }
    if (!out->terminated && !out->entered_cycle && !out->overflow) {
        out->final_value = current;
    }
    out->steps = steps;
    out->wall_time_ms = monotonic_millis() - start_ms;
    history_destroy(&hist);
}

static double compute_probe_score(const aliquot_outcome_t *out) {
    double span = (out->seed > 0) ? (double)out->max_value / (double)out->seed : 1.0;
    if (span < 1.0) span = 1.0;
    double log_height = log(span);
    double base = (double)out->steps * 0.75 + log_height * 8.0;
    if (out->hit_limit) base += 30.0;
    if (out->max_value > 1000000000ULL) base += 25.0;
    base += compute_overflow_pressure(out);
    return base;
}

static bool looks_long(const aliquot_outcome_t *out, double *score_out) {
    double score = compute_probe_score(out);
    if (score_out) *score_out = score;
    if (out->terminated || out->entered_cycle || out->overflow) return false;
    return score >= SCOUT_SCORE_GATE;
}

static double compute_overflow_pressure(const aliquot_outcome_t *out) {
    if (!out) return 0.0;
    if (out->overflow) {
        return 60.0;
    }
    long double ratio = (long double)out->max_value / (long double)UINT64_MAX;
    if (ratio < 0.0L) ratio = 0.0L;
    if (ratio > 1.0L) ratio = 1.0L;
    return (double)(ratio * 60.0L);
}

static void job_queue_init(job_queue_t *q) {
    memset(q, 0, sizeof(*q));
    ttak_mutex_init(&q->lock);
    ttak_cond_init(&q->cond);
}

static bool job_queue_push(job_queue_t *q, const aliquot_job_t *job) {
    ttak_mutex_lock(&q->lock);
    if (q->count == JOB_QUEUE_CAP) {
        ttak_mutex_unlock(&q->lock);
        return false;
    }
    q->buffer[q->tail] = *job;
    q->tail = (q->tail + 1) % JOB_QUEUE_CAP;
    q->count++;
    ttak_cond_signal(&q->cond);
    ttak_mutex_unlock(&q->lock);
    return true;
}

static bool job_queue_pop(job_queue_t *q, aliquot_job_t *job) {
    ttak_mutex_lock(&q->lock);
    while (q->count == 0 && !atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) {
        ttak_cond_wait(&q->cond, &q->lock);
    }
    if (q->count == 0) {
        ttak_mutex_unlock(&q->lock);
        return false;
    }
    *job = q->buffer[q->head];
    q->head = (q->head + 1) % JOB_QUEUE_CAP;
    q->count--;
    ttak_mutex_unlock(&q->lock);
    return true;
}

static size_t job_queue_count(job_queue_t *q) {
    ttak_mutex_lock(&q->lock);
    size_t count = q->count;
    ttak_mutex_unlock(&q->lock);
    return count;
}

static size_t job_queue_snapshot(job_queue_t *q, uint64_t *dest, size_t cap) {
    ttak_mutex_lock(&q->lock);
    size_t copied = 0;
    size_t idx = q->head;
    for (size_t i = 0; i < q->count && copied < cap; ++i) {
        dest[copied++] = q->buffer[idx].seed;
        idx = (idx + 1) % JOB_QUEUE_CAP;
    }
    ttak_mutex_unlock(&q->lock);
    return copied;
}

static bool ensure_found_capacity(size_t extra) {
    if (g_found_count + extra <= g_found_cap) return true;
    size_t new_cap = g_found_cap ? g_found_cap * 2 : 32;
    while (new_cap < g_found_count + extra) new_cap *= 2;
    size_t bytes = new_cap * sizeof(*g_found_records);
    uint64_t now = monotonic_millis();
    found_record_t *tmp = g_found_records
        ? ttak_mem_realloc(g_found_records, bytes, __TTAK_UNSAFE_MEM_FOREVER__, now)
        : ttak_mem_alloc(bytes, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!tmp) return false;
    g_found_records = tmp;
    g_found_cap = new_cap;
    return true;
}

static bool ensure_jump_capacity(size_t extra) {
    if (g_jump_count + extra <= g_jump_cap) return true;
    size_t new_cap = g_jump_cap ? g_jump_cap * 2 : 32;
    while (new_cap < g_jump_count + extra) new_cap *= 2;
    size_t bytes = new_cap * sizeof(*g_jump_records);
    uint64_t now = monotonic_millis();
    jump_record_t *tmp = g_jump_records
        ? ttak_mem_realloc(g_jump_records, bytes, __TTAK_UNSAFE_MEM_FOREVER__, now)
        : ttak_mem_alloc(bytes, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!tmp) return false;
    g_jump_records = tmp;
    g_jump_cap = new_cap;
    return true;
}

static bool ensure_track_capacity(size_t extra) {
    if (g_track_count + extra <= g_track_cap) return true;
    size_t new_cap = g_track_cap ? g_track_cap * 2 : 32;
    while (new_cap < g_track_count + extra) new_cap *= 2;
    size_t bytes = new_cap * sizeof(*g_track_records);
    uint64_t now = monotonic_millis();
    track_record_t *tmp = g_track_records
        ? ttak_mem_realloc(g_track_records, bytes, __TTAK_UNSAFE_MEM_FOREVER__, now)
        : ttak_mem_alloc(bytes, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!tmp) return false;
    g_track_records = tmp;
    g_track_cap = new_cap;
    return true;
}

static void append_found_record(const aliquot_outcome_t *out, const char *source) {
    if (!out) return;
    if (!ensure_found_capacity(1)) return;
    found_record_t *rec = &g_found_records[g_found_count++];
    rec->seed = out->seed;
    rec->steps = out->steps;
    rec->max_value = out->max_value;
    rec->final_value = out->final_value;
    rec->cycle_length = out->cycle_length;
    snprintf(rec->status, sizeof(rec->status), "%s", classify_outcome(out));
    if (source) {
        snprintf(rec->provenance, sizeof(rec->provenance), "%s", source);
    } else {
        rec->provenance[0] = '\0';
    }
    printf("[ALIQUOT] seed=%" PRIu64 " steps=%" PRIu64 " status=%s via %s\n",
           rec->seed, rec->steps, rec->status,
           rec->provenance[0] ? rec->provenance : "unknown");
    ttak_atomic_inc64(&g_total_sequences);
}

static void append_jump_record(uint64_t seed, uint64_t steps, uint64_t max_value, double score, double overflow_pressure) {
    if (!ensure_jump_capacity(1)) return;
    jump_record_t *rec = &g_jump_records[g_jump_count++];
    rec->seed = seed;
    rec->preview_steps = steps;
    rec->preview_max = max_value;
    rec->score = score;
    rec->overflow_pressure = overflow_pressure;
    printf("[SCOUT] seed=%" PRIu64 " steps=%" PRIu64 " max=%" PRIu64 " score=%.2f overflow=%.2f\n",
           seed, steps, max_value, score, overflow_pressure);
}

static const char *track_end_reason(const aliquot_outcome_t *out) {
    if (!out) return "unknown";
    if (out->overflow) return "overflow";
    if (out->catalog_hit) return "catalog";
    if (out->perfect) return "perfect";
    if (out->amicable) return "amicable";
    if (out->entered_cycle) return "cycle";
    if (out->terminated) return "terminated";
    if (out->time_budget_hit) return "time-budget";
    if (out->hit_limit) return "step-limit";
    return "open";
}

static void format_track_end_detail(const aliquot_outcome_t *out, char *dest, size_t dest_cap) {
    if (!dest || dest_cap == 0) return;
    if (!out) {
        snprintf(dest, dest_cap, "%s", "unknown");
        return;
    }
    if (out->overflow) {
        snprintf(dest, dest_cap, "%s", "overflow");
        return;
    }
    if (out->catalog_hit) {
        snprintf(dest, dest_cap, "%s", "catalog_hit");
        return;
    }
    if (out->time_budget_hit) {
        snprintf(dest, dest_cap, "%s", "time_budget");
        return;
    }
    if (out->entered_cycle) {
        if (out->cycle_length > 0) {
            snprintf(dest, dest_cap, "cycle_%u", out->cycle_length);
        } else {
            snprintf(dest, dest_cap, "%s", "cycle");
        }
        return;
    }
    if (out->terminated) {
        uint64_t final_value = out->final_value;
        snprintf(dest, dest_cap, "reached_%" PRIu64, final_value);
        return;
    }
    if (out->hit_limit) {
        snprintf(dest, dest_cap, "%s", "step_limit");
        return;
    }
    snprintf(dest, dest_cap, "%s", "open");
}

static uint32_t bigint_bit_length(const ttak_bigint_t *bi) {
    if (!bi || bi->used == 0) return 0;
    const limb_t *limbs = bi->is_dynamic ? bi->data.dyn_ptr : bi->data.sso_buf;
    limb_t top = limbs[bi->used - 1];
    uint32_t bits = (uint32_t)((bi->used - 1) * 32);
    if (top == 0) return bits;
#if defined(__GNUC__)
    bits += 32U - (uint32_t)__builtin_clz(top);
#else
    uint32_t local = 0;
    while (top) {
        top >>= 1;
        local++;
    }
    bits += local;
#endif
    return bits;
}

static uint32_t bigint_decimal_digits_estimate(uint32_t bits) {
    if (bits == 0) return 1;
    double approx = (double)bits * 0.3010299956639812; // log10(2)
    uint32_t digits = (uint32_t)approx + 1;
    return digits;
}

static bool bigint_export_u64(const ttak_bigint_t *bi, uint64_t *value_out) {
    if (!bi) return false;
    if (bi->used > 2) return false;
    const limb_t *limbs = bi->is_dynamic ? bi->data.dyn_ptr : bi->data.sso_buf;
    uint64_t value = 0;
    if (bi->used >= 1) value |= (uint64_t)limbs[0];
    if (bi->used == 2) value |= ((uint64_t)limbs[1] << 32);
    if (value_out) *value_out = value;
    return true;
}

static void bigint_format_prefix(const ttak_bigint_t *bi, char *dest, size_t dest_cap) {
    if (!dest || dest_cap == 0) return;
    dest[0] = '\0';
    uint64_t value = 0;
    if (!bigint_export_u64(bi, &value)) {
        return;
    }
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    size_t need = strlen(buffer);
    if (need > TRACK_PREFIX_DIGITS) need = TRACK_PREFIX_DIGITS;
    if (need >= dest_cap) need = dest_cap - 1;
    memcpy(dest, buffer, need);
    dest[need] = '\0';
}

static void bigint_hash_hex(const ttak_bigint_t *bi, char out[65]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    if (bi && bi->used > 0) {
        const limb_t *limbs = bi->is_dynamic ? bi->data.dyn_ptr : bi->data.sso_buf;
        sha256_update(&ctx, limbs, bi->used * sizeof(limb_t));
    }
    uint8_t digest[32];
    sha256_final(&ctx, digest);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        out[i * 2] = hex[(digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    out[64] = '\0';
}

static void capture_track_metrics(const aliquot_outcome_t *out, const aliquot_job_t *job, uint64_t budget_ms, track_record_t *rec) {
    if (!out || !rec) return;
    memset(rec, 0, sizeof(*rec));
    rec->seed = out->seed;
    rec->steps = out->steps;
    rec->wall_time_ms = out->wall_time_ms;
    rec->budget_ms = budget_ms;
    rec->max_u64 = out->max_value;
    rec->scout_score = job ? job->scout_score : 0.0;
    rec->priority = job ? job->priority : 0;
    snprintf(rec->ended, sizeof(rec->ended), "%s", track_end_reason(out));
    format_track_end_detail(out, rec->ended_by, sizeof(rec->ended_by));
    uint64_t now = monotonic_millis();
    ttak_bigint_t max_bi;
    ttak_bigint_init(&max_bi, now);
    if (ttak_bigint_set_u64(&max_bi, out->max_value, now)) {
        rec->max_bits = bigint_bit_length(&max_bi);
        rec->max_dec_digits = bigint_decimal_digits_estimate(rec->max_bits);
        bigint_hash_hex(&max_bi, rec->max_hash);
        bigint_format_prefix(&max_bi, rec->max_prefix, sizeof(rec->max_prefix));
    } else {
        rec->max_bits = 0;
        rec->max_dec_digits = 0;
        rec->max_hash[0] = '\0';
        rec->max_prefix[0] = '\0';
    }
    ttak_bigint_free(&max_bi, now);
}

static void append_track_record(const aliquot_outcome_t *out, const aliquot_job_t *job, uint64_t budget_ms) {
    if (!out) return;
    if (!ensure_track_capacity(1)) return;
    track_record_t *rec = &g_track_records[g_track_count++];
    capture_track_metrics(out, job, budget_ms, rec);
    printf("[TRACK] seed=%" PRIu64 " bits=%u ended_by=%s\n",
           rec->seed, rec->max_bits, rec->ended_by);
}

static uint64_t determine_time_budget(const aliquot_job_t *job) {
    if (!job) return TRACK_FAST_BUDGET_MS;
    if (job->priority >= 3) return TRACK_DEEP_BUDGET_MS;
    if (job->preview_overflow) return TRACK_DEEP_BUDGET_MS;
    if (job->scout_score >= SCOUT_SCORE_GATE * 1.5) return TRACK_DEEP_BUDGET_MS;
    return TRACK_FAST_BUDGET_MS;
}

static const uint32_t g_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[64]) {
    uint32_t m[64];
    for (int i = 0; i < 16; ++i) {
        m[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(m[i - 15], 7) ^ rotr32(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr32(m[i - 2], 17) ^ rotr32(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + g_sha256_k[i] + m[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    while (len > 0) {
        size_t space = 64 - ctx->used;
        size_t take = (len < space) ? len : space;
        memcpy(&ctx->buffer[ctx->used], bytes, take);
        ctx->used += take;
        bytes += take;
        len -= take;
        ctx->bitcount += (uint64_t)take * 8ULL;
        if (ctx->used == 64) {
            sha256_transform(ctx, ctx->buffer);
            ctx->used = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t out[32]) {
    uint64_t total_bits = ctx->bitcount;
    uint8_t one = 0x80;
    sha256_update(ctx, &one, 1);
    uint8_t zero = 0;
    while (ctx->used != 56) {
        sha256_update(ctx, &zero, 1);
    }
    uint8_t len_block[8];
    for (int i = 0; i < 8; ++i) {
        len_block[7 - i] = (uint8_t)(total_bits >> (i * 8));
    }
    sha256_update(ctx, len_block, 8);
    for (int i = 0; i < 8; ++i) {
        out[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
    memset(ctx, 0, sizeof(*ctx));
}

static void rehydrate_found_record(const found_record_t *rec) {
    if (!rec) return;
    if (!ensure_found_capacity(1)) return;
    g_found_records[g_found_count++] = *rec;
}

static void rehydrate_jump_record(const jump_record_t *rec) {
    if (!rec) return;
    if (!ensure_jump_capacity(1)) return;
    g_jump_records[g_jump_count++] = *rec;
}

static void rehydrate_track_record(const track_record_t *rec) {
    if (!rec) return;
    if (!ensure_track_capacity(1)) return;
    g_track_records[g_track_count++] = *rec;
}

static void persist_found_records(void) {
    FILE *fp = fopen(g_found_log_path, "w");
    if (!fp) {
        fprintf(stderr, "[ALIQUOT] Failed to write %s: %s\n", g_found_log_path, strerror(errno));
        return;
    }
    for (size_t i = 0; i < g_found_count; ++i) {
        const found_record_t *rec = &g_found_records[i];
        fprintf(fp, "{\"seed\":%" PRIu64 ",\"steps\":%" PRIu64 ",\"max\":%" PRIu64 ",\"final\":%" PRIu64 ",\"cycle\":%u,\"status\":\"%s\",\"source\":\"%s\"}\n",
                rec->seed, rec->steps, rec->max_value, rec->final_value,
                rec->cycle_length, rec->status, rec->provenance);
    }
    fclose(fp);
}

static void persist_jump_records(void) {
    FILE *fp = fopen(g_jump_log_path, "w");
    if (!fp) {
        fprintf(stderr, "[ALIQUOT] Failed to write %s: %s\n", g_jump_log_path, strerror(errno));
        return;
    }
    for (size_t i = 0; i < g_jump_count; ++i) {
        const jump_record_t *rec = &g_jump_records[i];
        fprintf(fp, "{\"seed\":%" PRIu64 ",\"steps\":%" PRIu64 ",\"max\":%" PRIu64 ",\"score\":%.2f,\"overflow\":%.3f}\n",
                rec->seed, rec->preview_steps, rec->preview_max, rec->score, rec->overflow_pressure);
    }
    fclose(fp);
}

static void persist_track_records(void) {
    FILE *fp = fopen(g_track_log_path, "w");
    if (!fp) {
        fprintf(stderr, "[ALIQUOT] Failed to write %s: %s\n", g_track_log_path, strerror(errno));
        return;
    }
    for (size_t i = 0; i < g_track_count; ++i) {
        const track_record_t *rec = &g_track_records[i];
        fprintf(fp,
                "{\"seed\":%" PRIu64 ",\"steps\":%" PRIu64 ",\"bits\":%u,\"digits\":%u,"
                "\"hash\":\"%s\",\"prefix\":\"%s\",\"ended\":\"%s\",\"ended_by\":\"%s\",\"wall_ms\":%" PRIu64 ","
                "\"budget_ms\":%" PRIu64 ",\"score\":%.2f,\"priority\":%u,\"max_u64\":%" PRIu64 "}\n",
                rec->seed, rec->steps, rec->max_bits, rec->max_dec_digits,
                rec->max_hash, rec->max_prefix, rec->ended, rec->ended_by, rec->wall_time_ms,
                rec->budget_ms, rec->scout_score, rec->priority, rec->max_u64);
    }
    fclose(fp);
}

static void persist_queue_state(void) {
    uint64_t snapshot[JOB_QUEUE_CAP];
    size_t count = job_queue_snapshot(&g_job_queue, snapshot, JOB_QUEUE_CAP);
    FILE *fp = fopen(g_queue_state_path, "w");
    if (!fp) {
        fprintf(stderr, "[ALIQUOT] Failed to write %s: %s\n", g_queue_state_path, strerror(errno));
        return;
    }
    fprintf(fp, "{\"pending\":[");
    for (size_t i = 0; i < count; ++i) {
        fprintf(fp, "%s%" PRIu64, (i == 0) ? "" : ",", snapshot[i]);
    }
    fprintf(fp, "],\"ts\":%" PRIu64 "}\n", monotonic_millis());
    fclose(fp);
}

static void flush_ledgers(void) {
    ttak_mutex_lock(&g_disk_lock);
    persist_found_records();
    persist_jump_records();
    persist_track_records();
    persist_queue_state();
    ttak_atomic_write64(&g_last_persist_ms, monotonic_millis());
    ttak_mutex_unlock(&g_disk_lock);
}

static void maybe_flush_ledgers(void) {
    uint64_t now = monotonic_millis();
    uint64_t last = ttak_atomic_read64(&g_last_persist_ms);
    if (now - last >= FLUSH_INTERVAL_MS) {
        flush_ledgers();
    }
}

static void load_found_records(void) {
    FILE *fp = fopen(g_found_log_path, "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        found_record_t rec = {0};
        if (sscanf(line, "{\"seed\":%" SCNu64 ",\"steps\":%" SCNu64 ",\"max\":%" SCNu64 ",\"final\":%" SCNu64 ",\"cycle\":%u,\"status\":\"%23[^\"]\",\"source\":\"%15[^\"]\"}",
                   &rec.seed, &rec.steps, &rec.max_value, &rec.final_value,
                   &rec.cycle_length, rec.status, rec.provenance) >= 5) {
            rehydrate_found_record(&rec);
            seed_registry_mark(rec.seed);
        }
    }
    fclose(fp);
}

static void load_jump_records(void) {
    FILE *fp = fopen(g_jump_log_path, "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        jump_record_t rec = {0};
        int matched = sscanf(line, "{\"seed\":%" SCNu64 ",\"steps\":%" SCNu64 ",\"max\":%" SCNu64 ",\"score\":%lf,\"overflow\":%lf",
                             &rec.seed, &rec.preview_steps, &rec.preview_max, &rec.score, &rec.overflow_pressure);
        if (matched >= 4) {
            rehydrate_jump_record(&rec);
        }
    }
    fclose(fp);
}

static void load_track_records(void) {
    FILE *fp = fopen(g_track_log_path, "r");
    if (!fp) return;
    char line[768];
    while (fgets(line, sizeof(line), fp)) {
        track_record_t rec = {0};
        int matched = sscanf(
            line,
            "{\"seed\":%" SCNu64 ",\"steps\":%" SCNu64 ",\"bits\":%u,\"digits\":%u,"
            "\"hash\":\"%64[^\"]\",\"prefix\":\"%48[^\"]\",\"ended\":\"%23[^\"]\","
            "\"ended_by\":\"%31[^\"]\",\"wall_ms\":%" SCNu64 ",\"budget_ms\":%" SCNu64 ","
            "\"score\":%lf,\"priority\":%u,\"max_u64\":%" SCNu64 "}",
            &rec.seed, &rec.steps, &rec.max_bits, &rec.max_dec_digits,
            rec.max_hash, rec.max_prefix, rec.ended, rec.ended_by,
            &rec.wall_time_ms, &rec.budget_ms, &rec.scout_score,
            &rec.priority, &rec.max_u64);
        if (matched < 13) {
            matched = sscanf(
                line,
                "{\"seed\":%" SCNu64 ",\"steps\":%" SCNu64 ",\"bits\":%u,\"digits\":%u,"
                "\"hash\":\"%64[^\"]\",\"prefix\":\"%48[^\"]\",\"ended\":\"%23[^\"]\","
                "\"wall_ms\":%" SCNu64 ",\"budget_ms\":%" SCNu64 ",\"score\":%lf,"
                "\"priority\":%u,\"max_u64\":%" SCNu64 "}",
                &rec.seed, &rec.steps, &rec.max_bits, &rec.max_dec_digits,
                rec.max_hash, rec.max_prefix, rec.ended,
                &rec.wall_time_ms, &rec.budget_ms, &rec.scout_score,
                &rec.priority, &rec.max_u64);
            if (matched >= 11) {
                snprintf(rec.ended_by, sizeof(rec.ended_by), "%s", rec.ended);
            }
        }
        if (matched >= 13 || (matched >= 11 && rec.ended_by[0] != '\0')) {
            rehydrate_track_record(&rec);
        }
    }
    fclose(fp);
}

static void load_queue_checkpoint(void) {
    FILE *fp = fopen(g_queue_state_path, "r");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (sz <= 0) {
        fclose(fp);
        return;
    }
    rewind(fp);
    uint64_t now = monotonic_millis();
    char *buffer = ttak_mem_alloc((size_t)sz + 1, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!buffer) {
        fclose(fp);
        return;
    }
    fread(buffer, 1, (size_t)sz, fp);
    buffer[sz] = '\0';
    fclose(fp);
    char *start = strchr(buffer, '[');
    char *end = start ? strchr(start, ']') : NULL;
    if (start && end && end > start) {
        char *ptr = start + 1;
        while (ptr < end) {
            while (ptr < end && !isdigit((unsigned char)*ptr)) ptr++;
            if (ptr >= end) break;
            uint64_t seed = strtoull(ptr, &ptr, 10);
            if (seed > 1 && seed_registry_try_add(seed)) {
                aliquot_job_t job = {.seed = seed};
                job.priority = 1;
                job.scout_score = 0.0;
                job.preview_steps = 0;
                job.preview_max = 0;
                job.preview_overflow = false;
                snprintf(job.provenance, sizeof(job.provenance), "%s", "checkpoint");
                job_queue_push(&g_job_queue, &job);
            }
        }
    }
    ttak_mem_free(buffer);
}

static void process_job(const aliquot_job_t *job) {
    if (!job) return;
    aliquot_outcome_t outcome;
    uint64_t budget_ms = determine_time_budget(job);
    run_aliquot_sequence(job->seed, LONG_RUN_MAX_STEPS, budget_ms, &outcome);
    append_found_record(&outcome, job->provenance);
    append_track_record(&outcome, job, budget_ms);
    maybe_flush_ledgers();
}

static void *worker_main(void *arg) {
    worker_ctx_t *ctx = (worker_ctx_t *)arg;
    while (!atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) {
        aliquot_job_t job;
        if (!job_queue_pop(&g_job_queue, &job)) {
            if (atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) break;
            continue;
        }
        process_job(&job);
        atomic_fetch_add(&ctx->op_count, 1);
    }
    return NULL;
}

static void *scout_main(void *arg) {
    (void)arg;
    while (!atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) {
        size_t depth = job_queue_count(&g_job_queue);
        if (depth > JOB_QUEUE_CAP - 8) {
            responsive_sleep(SCOUT_SLEEP_MS);
            continue;
        }
        uint64_t seed = random_seed_between(SCOUT_MIN_SEED, SCOUT_MAX_SEED);
        if (!seed_registry_try_add(seed)) {
            responsive_sleep(10);
            continue;
        }
        scan_result_t scan_result;
        if (!frontier_accept_seed(seed, &scan_result)) {
            if (scan_result.ended_by == SCAN_END_CATALOG) {
                printf("[SCAN] filtered catalog seed=%" PRIu64 " steps=%" PRIu64 "\n",
                       seed, scan_result.steps);
            }
            responsive_sleep(5);
            continue;
        }
        aliquot_outcome_t probe;
        run_aliquot_sequence(seed, SCOUT_PREVIEW_STEPS, 0, &probe);
        double score = 0.0;
        double overflow_pressure = compute_overflow_pressure(&probe);
        if (looks_long(&probe, &score)) {
            append_jump_record(seed, probe.steps, probe.max_value, score, overflow_pressure);
            aliquot_job_t job = {.seed = seed};
            job.priority = (probe.overflow || overflow_pressure >= 45.0) ? 3 : 2;
            job.preview_steps = probe.steps;
            job.preview_max = probe.max_value;
            job.preview_overflow = probe.overflow || (overflow_pressure >= 45.0);
            job.scout_score = score;
            snprintf(job.provenance, sizeof(job.provenance), "%s", "scout");
            if (!job_queue_push(&g_job_queue, &job)) {
                fprintf(stderr, "[SCOUT] Queue rejected seed %" PRIu64 "\n", seed);
            }
            maybe_flush_ledgers();
        }
        responsive_sleep(SCOUT_SLEEP_MS);
    }
    return NULL;
}

int main(void) {
    printf("[ALIQUOT] Booting aliquot tracker...\n");
    seed_rng();
    configure_state_paths();
    ensure_state_dir();
    init_catalog_filters();
    printf("[ALIQUOT] Checkpoints at %s\n", g_state_dir);
    job_queue_init(&g_job_queue);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    ttak_atomic_write64(&g_last_persist_ms, monotonic_millis());

    load_found_records();
    load_jump_records();
    load_track_records();
    load_queue_checkpoint();

    pthread_t scout_thread;
    pthread_create(&scout_thread, NULL, scout_main, NULL);

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1) cpus = 1;
    if (cpus > MAX_WORKERS) cpus = MAX_WORKERS;
    g_worker_count = (int)cpus;

    for (int i = 0; i < g_worker_count; ++i) {
        g_workers[i].id = (uint32_t)i;
        atomic_store(&g_workers[i].op_count, 0);
        pthread_create(&g_workers[i].thread, NULL, worker_main, &g_workers[i]);
    }

    while (!atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) {
        responsive_sleep(1000);
        maybe_flush_ledgers();
        size_t queue_depth = job_queue_count(&g_job_queue);
        uint64_t completed = ttak_atomic_read64(&g_total_sequences);
        printf("[ALIQUOT] queue=%zu completed=%" PRIu64 " probes=%" PRIu64 "\n",
               queue_depth, completed, ttak_atomic_read64(&g_total_probes));
    }

    pthread_join(scout_thread, NULL);
    for (int i = 0; i < g_worker_count; ++i) {
        pthread_join(g_workers[i].thread, NULL);
    }

    flush_ledgers();
    printf("[ALIQUOT] Shutdown complete.\n");
    return 0;
}
