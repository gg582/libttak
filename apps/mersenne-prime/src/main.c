#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <ctype.h>

/* Internal TTAK Headers */
#include <ttak/mem/mem.h>
#include <ttak/math/ntt.h>
#include <ttak/timing/timing.h>
#include <ttak/atomic/atomic.h>
#include "../internal/app_types.h"
#include "hwinfo.h"

/* File Paths for Persistence */
#define CHECKPOINT_FILE    "/home/yjlee/Documents/mersenne_checkpoint.json"
#define LAST_FINISHED_FILE "/home/yjlee/Documents/mersenne_last.json"
#define MAX_WORKERS 12

/* Global Atomic States */
static volatile atomic_bool shutdown_requested = false;
static atomic_uint g_next_p;
static atomic_uint g_max_finished_p = 0; // Tracks the highest exponent that actually FINISHED
static ttak_hw_spec_t g_hw_spec;
static app_state_t *g_app_state = NULL;
static uint64_t g_start_tick;

typedef struct {
    alignas(64) atomic_uint_fast64_t ops_count;
    uint32_t id;
} worker_ctx_t;

static worker_ctx_t g_workers[MAX_WORKERS];
static int g_num_workers = 4;

typedef struct {
    uint32_t p;
    uint64_t residue;
    bool is_prime;
} verification_record_t;

static verification_record_t *g_verification_log = NULL;
static size_t g_verification_log_count = 0;
static size_t g_verification_log_capacity = 0;
static pthread_mutex_t g_verification_log_lock = PTHREAD_MUTEX_INITIALIZER;

static bool load_uint_from_file(const char *filename, const char *key, uint32_t *out);
static uint32_t load_checkpoint_value(const char *filename, const char *primary_key,
                                      const char *fallback_key, uint32_t default_value);
static void responsive_sleep(uint32_t milliseconds);
static uint64_t collect_total_ops(void);

/* External functions for file I/O and reporting */
extern void save_current_progress(const char *filename, const void *data, size_t size);
extern int report_to_gimps(app_state_t *state, const gimps_result_t *res, const ttak_node_telemetry_t *telemetry);
extern void generate_computer_id(char *buf, size_t len);

static void handle_signal(int sig) {
    (void)sig;
    atomic_store(&shutdown_requested, true);
}

static bool load_uint_from_file(const char *filename, const char *key, uint32_t *out) {
    if (!filename || !key || !out) return false;
    FILE *f = fopen(filename, "r");
    if (!f) return false;
    char line[256];
    size_t key_len = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        char *needle = strstr(line, key);
        if (!needle) continue;
        needle += key_len;
        char *colon = strchr(needle, ':');
        if (!colon) continue;
        colon++;
        while (*colon && !isdigit((unsigned char)*colon) && *colon != '-') colon++;
        if (!*colon) continue;
        *out = (uint32_t)strtoul(colon, NULL, 10);
        fclose(f);
        return true;
    }
    fclose(f);
    return false;
}

static uint32_t load_checkpoint_value(const char *filename, const char *primary_key,
                                      const char *fallback_key, uint32_t default_value) {
    uint32_t value = 0;
    if (primary_key && load_uint_from_file(filename, primary_key, &value)) return value;
    if (fallback_key && load_uint_from_file(filename, fallback_key, &value)) return value;
    return default_value;
}

static void responsive_sleep(uint32_t milliseconds) {
    const uint32_t step_ms = 250;
    uint32_t waited = 0;
    while (waited < milliseconds && !atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) {
        uint32_t chunk = milliseconds - waited;
        if (chunk > step_ms) chunk = step_ms;
        usleep(chunk * 1000);
        waited += chunk;
    }
}

static uint64_t collect_total_ops(void) {
    uint64_t total = 0;
    for (int i = 0; i < g_num_workers; i++) {
        total += atomic_load_explicit(&g_workers[i].ops_count, memory_order_relaxed);
    }
    return total;
}

static void append_verification_record(uint32_t p, uint64_t residue, bool is_prime) {
    pthread_mutex_lock(&g_verification_log_lock);
    if (g_verification_log_count == g_verification_log_capacity) {
        size_t new_cap = g_verification_log_capacity ? g_verification_log_capacity * 2 : 32;
        verification_record_t *new_entries = g_verification_log
            ? (verification_record_t *)ttak_mem_realloc(g_verification_log, new_cap * sizeof(*new_entries), __TTAK_UNSAFE_MEM_FOREVER__, 0)
            : (verification_record_t *)ttak_mem_alloc(new_cap * sizeof(*new_entries), __TTAK_UNSAFE_MEM_FOREVER__, 0);
        if (!new_entries) {
            pthread_mutex_unlock(&g_verification_log_lock);
            return;
        }
        g_verification_log = new_entries;
        g_verification_log_capacity = new_cap;
    }
    g_verification_log[g_verification_log_count++] = (verification_record_t){
        .p = p,
        .residue = residue,
        .is_prime = is_prime
    };
    pthread_mutex_unlock(&g_verification_log_lock);
}

static int append_json_segment(char **buf, size_t *cap, size_t *len, const char *fmt, ...) {
    if (!buf || !cap || !len) return -1;
    if (!*buf) {
        *cap = 256;
        *len = 0;
        *buf = (char *)ttak_mem_alloc(*cap, __TTAK_UNSAFE_MEM_FOREVER__, 0);
        if (!*buf) return -1;
        (*buf)[0] = '\0';
    }
    while (1) {
        if (*cap - *len <= 1) {
            size_t new_cap = (*cap == 0) ? 256 : (*cap * 2);
            char *tmp = (char *)ttak_mem_realloc(*buf, new_cap, __TTAK_UNSAFE_MEM_FOREVER__, 0);
            if (!tmp) return -1;
            *buf = tmp;
            *cap = new_cap;
        }
        va_list args;
        va_start(args, fmt);
        int written = vsnprintf(*buf + *len, *cap - *len, fmt, args);
        va_end(args);
        if (written < 0) return -1;
        if ((size_t)written < (*cap - *len)) {
            *len += (size_t)written;
            return 0;
        }
        size_t required = *len + (size_t)written + 1;
        size_t new_cap = (*cap == 0) ? required : (*cap * 2);
        if (new_cap < required) new_cap = required;
        char *tmp = (char *)ttak_mem_realloc(*buf, new_cap, __TTAK_UNSAFE_MEM_FOREVER__, 0);
        if (!tmp) return -1;
        *buf = tmp;
        *cap = new_cap;
    }
}

static char *build_last_results_json(uint32_t max_finished_p, uint64_t total_ops, size_t *out_size) {
    pthread_mutex_lock(&g_verification_log_lock);
    char *buf = NULL;
    size_t cap = 0, len = 0;
    if (append_json_segment(&buf, &cap, &len,
            "{\n    \"max_finished_p\": %u,\n    \"total_ops\": %" PRIu64 ",\n    \"results\": [\n",
            max_finished_p, (uint64_t)total_ops) != 0) goto fail;
    for (size_t i = 0; i < g_verification_log_count; i++) {
        verification_record_t rec = g_verification_log[i];
        const char *sep = (i + 1 == g_verification_log_count) ? "" : ",";
        if (append_json_segment(&buf, &cap, &len,
                "        {\"p\": %u, \"residue\": \"0x%016" PRIx64 "\", \"is_prime\": %s}%s\n",
                rec.p, rec.residue, rec.is_prime ? "true" : "false", sep) != 0) {
            goto fail;
        }
    }
    if (append_json_segment(&buf, &cap, &len, "    ]\n}\n") != 0) goto fail;
    pthread_mutex_unlock(&g_verification_log_lock);
    if (out_size) *out_size = len;
    return buf;
fail:
    if (buf) ttak_mem_free(buf);
    pthread_mutex_unlock(&g_verification_log_lock);
    return NULL;
}

/**
 * @brief Lucas-Lehmer test core.
 * @return 1 if prime, 0 if composite, -1 if aborted.
 */
static int ttak_ll_test_core(uint32_t p, uint64_t *out_residue) {
    if (p == 2) { *out_residue = 0; return 1; }

    size_t n = (p + 63) / 64;
    size_t ntt_size = ttak_next_power_of_two(n * 2);

    uint64_t *s_words = (uint64_t *)ttak_mem_alloc_safe(ntt_size * sizeof(uint64_t), 0, 0, false, false, true, TTAK_MEM_CACHE_ALIGNED);
    uint64_t *tmp_res[TTAK_NTT_PRIME_COUNT];
    for (int k = 0; k < TTAK_NTT_PRIME_COUNT; k++) {
        tmp_res[k] = (uint64_t *)ttak_mem_alloc_safe(ntt_size * sizeof(uint64_t), 0, 0, false, false, true, TTAK_MEM_CACHE_ALIGNED);
    }

    s_words[0] = 4;
    for (uint32_t i = 0; i < p - 2; i++) {
        if (atomic_load(&shutdown_requested)) goto cancel;

        for (int k = 0; k < TTAK_NTT_PRIME_COUNT; k++) {
            memcpy(tmp_res[k], s_words, n * sizeof(uint64_t));
            if (ntt_size > n) memset(tmp_res[k] + n, 0, (ntt_size - n) * sizeof(uint64_t));
            ttak_ntt_transform(tmp_res[k], ntt_size, &ttak_ntt_primes[k], false);
            ttak_ntt_pointwise_square(tmp_res[k], tmp_res[k], ntt_size, &ttak_ntt_primes[k]);
            ttak_ntt_transform(tmp_res[k], ntt_size, &ttak_ntt_primes[k], true);
        }

        unsigned __int128 carry = 0;
        for (size_t j = 0; j < ntt_size; j++) {
            ttak_crt_term_t terms[TTAK_NTT_PRIME_COUNT];
            for (int k = 0; k < TTAK_NTT_PRIME_COUNT; k++) {
                terms[k].modulus = ttak_ntt_primes[k].modulus;
                terms[k].residue = tmp_res[k][j];
            }
            ttak_u128_t r, m;
            ttak_crt_combine(terms, TTAK_NTT_PRIME_COUNT, &r, &m);
            unsigned __int128 v = ((unsigned __int128)r.hi << 64) | r.lo;
            v += carry;
            s_words[j] = (uint64_t)v;
            carry = v >> 64;
        }
        if (s_words[0] >= 2) s_words[0] -= 2;
        else s_words[0] = s_words[0] + (uint64_t)-1 - 2;
    }

    *out_residue = s_words[0];
    int res = (s_words[0] == 0);

cleanup:
    ttak_mem_free(s_words);
    for (int k = 0; k < TTAK_NTT_PRIME_COUNT; k++) ttak_mem_free(tmp_res[k]);
    return res;
cancel:
    res = -1;
    goto cleanup;
}

void* worker_thread(void* arg) {
    worker_ctx_t *ctx = (worker_ctx_t*)arg;
    while (!atomic_load(&shutdown_requested)) {
        uint32_t p = atomic_fetch_add(&g_next_p, 2);

        /* Quick primality check for exponent p */
        bool p_is_prime = true;
        if (p < 2) p_is_prime = false;
        else if (p % 2 == 0) p_is_prime = (p == 2);
        else {
            for (uint32_t i = 3; i * i <= p; i += 2) {
                if (p % i == 0) { p_is_prime = false; break; }
            }
        }
        if (!p_is_prime) continue;

        printf("[WORKER %u] Starting LL Test for p: %u\n", ctx->id, p);
        uint64_t st = ttak_get_tick_count(), res_v = 0;
        int is_p = ttak_ll_test_core(p, &res_v);
        uint64_t et = ttak_get_tick_count();

        if (is_p != -1) {
            gimps_result_t result = { .p = p, .residue = res_v, .is_prime = (is_p == 1) };
            ttak_node_telemetry_t tel = {0};
            ttak_collect_hw_spec(&tel.spec);
            tel.exponent_in_progress = p;
            tel.iteration_time_ms = (et - st);
            tel.uptime_seconds = (double)(et - g_start_tick) / 1000.0;
            tel.active_workers = (uint32_t)g_num_workers;
            tel.total_ops = atomic_load_explicit(&ctx->ops_count, memory_order_relaxed);
            snprintf(tel.residual_snapshot, sizeof(tel.residual_snapshot), "%016" PRIx64, res_v);

            report_to_gimps(g_app_state, &result, &tel);

            /* Thread-safe update of the maximum verified exponent */
            uint32_t cur_max = atomic_load(&g_max_finished_p);
            while (p > cur_max) {
                if (atomic_compare_exchange_weak(&g_max_finished_p, &cur_max, p)) break;
            }

            append_verification_record(p, res_v, is_p == 1);
        }
        atomic_fetch_add_explicit(&ctx->ops_count, 1, memory_order_relaxed);
    }
    return NULL;
}

int main(int argc, char **argv) {
    curl_global_init(CURL_GLOBAL_ALL);
    g_start_tick = ttak_get_tick_count();
    if (argc > 1) g_num_workers = atoi(argv[1]);

    struct sigaction sa = {.sa_handler = handle_signal};
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    ttak_collect_hw_spec(&g_hw_spec);
    mkdir("/home/yjlee/Documents", 0755);

    g_app_state = (app_state_t *)ttak_mem_alloc_safe(sizeof(app_state_t), 0, 0, false, false, true, TTAK_MEM_CACHE_ALIGNED);
    generate_computer_id(g_app_state->computerid, sizeof(g_app_state->computerid));
    strncpy(g_app_state->userid, "anonymous", sizeof(g_app_state->userid) - 1);

    /* Restore dispatcher and finished checkpoints */
    uint32_t resume_p = load_checkpoint_value(CHECKPOINT_FILE, "\"last_p\"", NULL, 3);
    uint32_t start_p = (resume_p % 2 == 0) ? resume_p + 1 : resume_p;
    uint32_t persisted_max = load_checkpoint_value(LAST_FINISHED_FILE, "\"max_finished_p\"", "\"last_p\"", start_p);
    if (persisted_max > start_p) persisted_max = start_p;
    if (start_p <= persisted_max) {
        start_p = (persisted_max % 2 == 0) ? persisted_max + 1 : persisted_max + 2;
    }
    atomic_store(&g_next_p, start_p);
    atomic_store(&g_max_finished_p, persisted_max);
    printf("[SYSTEM] Initializing Engine. Next dispatch p: %u | Last verified p: %u\n",
           start_p, persisted_max);

    pthread_t th[MAX_WORKERS];
    for (int i = 0; i < g_num_workers; i++) {
        g_workers[i].id = i;
        atomic_init(&g_workers[i].ops_count, 0);
        pthread_create(&th[i], NULL, worker_thread, &g_workers[i]);
    }

    while (!atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) {
        responsive_sleep(60000);
        if (atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) break;

        uint64_t total_ops = collect_total_ops();
        uint32_t dispatch_p = atomic_load_explicit(&g_next_p, memory_order_relaxed);
        uint32_t finished_p = atomic_load_explicit(&g_max_finished_p, memory_order_relaxed);
        printf("[SYSTEM] Dispatching: %u | Max Verified: %u | Total Ops: %" PRIu64 "\n",
               dispatch_p, finished_p, total_ops);
        fflush(stdout);

        /* Save next dispatch point for checkpoint resume */
        char checkpoint_json[256];
        snprintf(checkpoint_json, sizeof(checkpoint_json),
                 "{\n    \"last_p\": %u,\n    \"total_ops\": %" PRIu64 "\n}\n",
                 dispatch_p, total_ops);
        save_current_progress(CHECKPOINT_FILE, checkpoint_json, strlen(checkpoint_json));

        /* Save full verification log with persisted max info */
        size_t last_json_size = 0;
        char *last_json = build_last_results_json(finished_p, total_ops, &last_json_size);
        if (last_json) {
            save_current_progress(LAST_FINISHED_FILE, last_json, last_json_size);
            ttak_mem_free(last_json);
        }
    }

    for (int i = 0; i < g_num_workers; i++) pthread_join(th[i], NULL);
    uint32_t final_dispatch = atomic_load_explicit(&g_next_p, memory_order_relaxed);
    uint64_t final_ops = collect_total_ops();
    uint32_t final_finished = atomic_load_explicit(&g_max_finished_p, memory_order_relaxed);

    char final_checkpoint[256];
    snprintf(final_checkpoint, sizeof(final_checkpoint),
             "{\n    \"last_p\": %u,\n    \"total_ops\": %" PRIu64 "\n}\n",
             final_dispatch, final_ops);
    save_current_progress(CHECKPOINT_FILE, final_checkpoint, strlen(final_checkpoint));

    size_t final_json_size = 0;
    char *final_last_json = build_last_results_json(final_finished, final_ops, &final_json_size);
    if (final_last_json) {
        save_current_progress(LAST_FINISHED_FILE, final_last_json, final_json_size);
        ttak_mem_free(final_last_json);
    }
    ttak_mem_free(g_app_state);
    if (g_verification_log) ttak_mem_free(g_verification_log);
    curl_global_cleanup();
    return 0;
}
