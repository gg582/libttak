/**
 * @file main.c
 * @brief Standalone aliquot tracker that seeds exploratory jobs, schedules work,
 *        and records interesting findings to disk.
 *
 * The file is lengthy because it keeps the scheduling, persistence, and arithmetic
 * glue in one place; concise comments mark the pieces that tend to trip maintainers.
 */
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
#include <ttak/math/factor.h>
#include <ttak/math/sum_divisors.h>
#include <ttak/thread/pool.h>

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
#define TRACK_DEEP_BUDGET_MS (365ULL * 24ULL * 60ULL * 60ULL * 1000ULL)
#define CATALOG_MAX_EXACT    512
#define CATALOG_MAX_MOD_RULE 256
#define SEED_REGISTRY_BUCKETS 65536

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
    uint32_t max_bits;
    char max_hash[65];
    char max_prefix[TRACK_PREFIX_DIGITS + 1];
    uint32_t max_dec_digits;
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

typedef struct history_entry {
    uint64_t value;
    uint32_t step;
    struct history_entry *next;
} history_entry_t;

typedef struct {
    history_entry_t *buckets[HISTORY_BUCKETS];
} history_table_t;

typedef struct history_big_entry {
    uint8_t hash[32];
    uint32_t step;
    struct history_big_entry *next;
} history_big_entry_t;

typedef struct {
    history_big_entry_t *buckets[HISTORY_BUCKETS];
} history_big_table_t;

typedef struct {
    uint64_t seed;
    char provenance[16];
    uint32_t priority;
    double scout_score;
    uint64_t preview_steps;
    uint64_t preview_max;
    bool preview_overflow;
} aliquot_job_t;

typedef struct seed_entry {
    uint64_t seed;
    struct seed_entry *next;
} seed_entry_t;

typedef struct {
    uint64_t seeds[JOB_QUEUE_CAP];
    size_t count;
} pending_queue_t;

static atomic_bool shutdown_requested;
static atomic_uint_fast64_t g_rng_state;

static char g_state_dir[PATH_MAX];
static char g_found_log_path[PATH_MAX];
static char g_jump_log_path[PATH_MAX];
static char g_track_log_path[PATH_MAX];
static char g_queue_state_path[PATH_MAX];

static seed_entry_t *g_seed_buckets[SEED_REGISTRY_BUCKETS];
static ttak_mutex_t g_seed_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t g_catalog_exact[CATALOG_MAX_EXACT];
static size_t g_catalog_exact_count;
static catalog_mod_rule_t g_catalog_mod_rules[CATALOG_MAX_MOD_RULE];
static size_t g_catalog_mod_rule_count;

static uint32_t bigint_decimal_digits_estimate(uint32_t bits);

/* Seeds that are already well documented elsewhere; we short-circuit them early. */
static const uint64_t g_catalog_seeds[] = {
    1, 2, 3, 4, 5, 6, 28, 496, 8128, 33550336,
    8589869056ULL, 137438691328ULL,
    1184, 1210, 2620, 2924, 5020, 5564, 6232, 6368,
    10744, 10856, 12285, 14595, 17296, 18416,
    24608, 27664, 45872, 45946, 66928, 66992,
    67095, 71145, 69615, 87633, 100485, 124155,
    122265, 139815, 141664, 153176, 142310, 168730,
    171856, 176336, 180848, 185368, 196724, 202444,
    280540, 365084, 308620, 389924, 418904, 748210,
    823816, 876960, 998104, 1154450, 1189800, 1866152,
    2082464, 2236570, 2652728, 2723792, 5224050, 5947064,
    6086552, 6175984
};

static found_record_t *g_found_records;
static size_t g_found_count;
static size_t g_found_cap;
static size_t g_persisted_found_count;

static jump_record_t *g_jump_records;
static size_t g_jump_count;
static size_t g_jump_cap;
static size_t g_persisted_jump_count;

static track_record_t *g_track_records;
static size_t g_track_count;
static size_t g_track_cap;
static size_t g_persisted_track_count;

static pending_queue_t g_pending_queue;
static ttak_mutex_t g_pending_lock = PTHREAD_MUTEX_INITIALIZER;

static ttak_mutex_t g_disk_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_last_persist_ms;
static uint64_t g_total_sequences;
static uint64_t g_total_probes;

static ttak_thread_pool_t *g_thread_pool;

static double compute_overflow_pressure(const aliquot_outcome_t *out);
static void *worker_process_job_wrapper(void *arg);
static void process_job(const aliquot_job_t *job);
static bool enqueue_job(aliquot_job_t *job, const char *source_tag);
static uint32_t bit_length_u64(uint64_t value);

static bool pending_queue_add(uint64_t seed) {
    ttak_mutex_lock(&g_pending_lock);
    if (g_pending_queue.count >= JOB_QUEUE_CAP) {
        ttak_mutex_unlock(&g_pending_lock);
        return false;
    }
    g_pending_queue.seeds[g_pending_queue.count++] = seed;
    ttak_mutex_unlock(&g_pending_lock);
    return true;
}

static void pending_queue_remove(uint64_t seed) {
    ttak_mutex_lock(&g_pending_lock);
    for (size_t i = 0; i < g_pending_queue.count; ++i) {
        if (g_pending_queue.seeds[i] == seed) {
            /* O(1) removal keeps snapshots cheap; ordering is irrelevant. */
            g_pending_queue.seeds[i] = g_pending_queue.seeds[g_pending_queue.count - 1];
            g_pending_queue.count--;
            break;
        }
    }
    ttak_mutex_unlock(&g_pending_lock);
}

static size_t pending_queue_snapshot(uint64_t *dest, size_t cap) {
    if (!dest || cap == 0) return 0;
    ttak_mutex_lock(&g_pending_lock);
    size_t count = g_pending_queue.count;
    if (count > cap) count = cap;
    memcpy(dest, g_pending_queue.seeds, count * sizeof(uint64_t));
    ttak_mutex_unlock(&g_pending_lock);
    return count;
}

static size_t pending_queue_depth(void) {
    ttak_mutex_lock(&g_pending_lock);
    size_t count = g_pending_queue.count;
    ttak_mutex_unlock(&g_pending_lock);
    return count;
}

static void handle_signal(int sig) {
    (void)sig;
    atomic_store_explicit(&shutdown_requested, true, memory_order_relaxed);
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

static uint32_t bit_length_u64(uint64_t value) {
    if (value == 0) return 0;
#if defined(__GNUC__) || defined(__clang__)
    return 64U - (uint32_t)__builtin_clzll(value);
#else
    uint32_t bits = 0;
    while (value) {
        bits++;
        value >>= 1;
    }
    return bits;
#endif
}

static void ensure_state_dir(void) {
    struct stat st;
    if (stat(g_state_dir, &st) == 0 && S_ISDIR(st.st_mode)) return;
    if (mkdir(g_state_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[ALIQUOT] Failed to create %s: %s\n", g_state_dir, strerror(errno));
    }
}

static bool sum_proper_divisors_u64_checked(uint64_t n, uint64_t *out) {
    if (!out) return false;
    return ttak_sum_proper_divisors_u64(n, out);
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
    /* These nodes live for the duration of the run, so the FOREVER tag is fine. */
    history_entry_t *node = ttak_mem_alloc(sizeof(*node), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) return;
    node->value = value;
    node->step = step;
    node->next = t->buckets[idx];
    t->buckets[idx] = node;
}

/* Avoid rescheduling the same seed by tracking it in a coarse hash table. */
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

static void history_big_init(history_big_table_t *t) {
    memset(t, 0, sizeof(*t));
}

static void history_big_destroy(history_big_table_t *t) {
    for (size_t i = 0; i < HISTORY_BUCKETS; ++i) {
        history_big_entry_t *node = t->buckets[i];
        while (node) {
            history_big_entry_t *next = node->next;
            ttak_mem_free(node);
            node = next;
        }
        t->buckets[i] = NULL;
    }
}

static bool history_big_contains(history_big_table_t *t, const ttak_bigint_t *value, uint32_t *step_out) {
    uint8_t hash[32];
    ttak_bigint_hash(value, hash);
    uint64_t hash_prefix;
    memcpy(&hash_prefix, hash, sizeof(hash_prefix));
    size_t idx = hash_prefix % HISTORY_BUCKETS;
    history_big_entry_t *node = t->buckets[idx];
    while (node) {
        if (memcmp(node->hash, hash, 32) == 0) {
            if (step_out) *step_out = node->step;
            return true;
        }
        node = node->next;
    }
    return false;
}

static void history_big_insert(history_big_table_t *t, const ttak_bigint_t *value, uint32_t step) {
    uint8_t hash[32];
    ttak_bigint_hash(value, hash);
    uint64_t hash_prefix;
    memcpy(&hash_prefix, hash, sizeof(hash_prefix));
    size_t idx = hash_prefix % HISTORY_BUCKETS;
    uint64_t now = monotonic_millis();
    history_big_entry_t *node = ttak_mem_alloc(sizeof(*node), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) return;
    memcpy(node->hash, hash, 32);
    node->step = step;
    node->next = t->buckets[idx];
    t->buckets[idx] = node;
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
    if (out->max_bits > 64) {
        if (out->entered_cycle) return "big-cycle";
        if (out->terminated) return "big-terminated";
        if (out->hit_limit) return "big-open-limit";
        return "big-open";
    }
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
        if (atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) break;
        if (SCAN_TIMECAP_MS > 0) {
            uint64_t now = monotonic_millis();
            if (now - start_ms >= (uint64_t)SCAN_TIMECAP_MS) break;
        }
        uint64_t next = 0;
        bool ok = sum_proper_divisors_u64_checked(current, &next);
        ttak_atomic_inc64(&g_total_probes);
        steps++;

        if (!ok) { // Overflow
            if (result) {
                result->ended_by = SCAN_END_OVERFLOW;
                result->steps = steps;
                result->max_u64 = max_value;
            }
            return true;
        }

        if (next > max_value) max_value = next;

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

static bool sum_proper_divisors_big(const ttak_bigint_t *n, ttak_bigint_t *result, uint64_t now) {
    return ttak_sum_proper_divisors_big(n, result, now);
}

static void run_aliquot_sequence_big(ttak_bigint_t *start_val, uint32_t start_step, uint32_t max_steps, uint64_t time_budget_ms, aliquot_outcome_t *out, uint64_t start_ms) {
    history_big_table_t hist;
    history_big_init(&hist);

    uint64_t now = monotonic_millis();
    ttak_bigint_t current;
    ttak_bigint_init_copy(&current, start_val, now);
    history_big_insert(&hist, &current, start_step);

    ttak_bigint_t max_seen;
    ttak_bigint_init_copy(&max_seen, start_val, now);
    // The u64 max value is stale. The starting big value is our new max.
    out->max_bits = ttak_bigint_get_bit_length(&max_seen);

    uint32_t steps = start_step;
    while (true) {
        if (ttak_bigint_cmp(&current, &max_seen) > 0) {
            ttak_bigint_copy(&max_seen, &current, now);
            out->max_bits = ttak_bigint_get_bit_length(&max_seen);
        }

        if (max_steps > 0 && steps >= max_steps) {
            out->hit_limit = true;
            break;
        }
        if (time_budget_ms > 0) {
            if (monotonic_millis() - start_ms >= time_budget_ms) {
                out->hit_limit = true;
                out->time_budget_hit = true;
                break;
            }
        }
        if (atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) break;

        now = monotonic_millis();
        ttak_bigint_t next;
        ttak_bigint_init(&next, now);

        uint64_t dbg_start_ms = monotonic_millis();
        sum_proper_divisors_big(&current, &next, now);
        uint64_t dbg_elapsed_ms = monotonic_millis() - dbg_start_ms;
        if (dbg_elapsed_ms > 1000) {
            printf("[ALIQUOT] slow big sum_divisors on a %zu-bit number took %" PRIu64 "ms\n", ttak_bigint_get_bit_length(&current), dbg_elapsed_ms);
        }

        ttak_atomic_inc64(&g_total_probes);
        steps++;

        if (ttak_bigint_is_zero(&next) || ttak_bigint_cmp_u64(&next, 1) == 0) {
            out->terminated = true;
            if (!ttak_bigint_export_u64(&next, &out->final_value)) {
                out->final_value = UINT64_MAX;
            }
            ttak_bigint_free(&next, now);
            break;
        }

        uint32_t prev_step = 0;
        if (history_big_contains(&hist, &next, &prev_step)) {
            out->entered_cycle = true;
            out->cycle_length = steps - prev_step;
            if (!ttak_bigint_export_u64(&next, &out->final_value)) {
                out->final_value = UINT64_MAX;
            }
            ttak_bigint_free(&next, now);
            break;
        }

        history_big_insert(&hist, &next, steps);
        ttak_bigint_copy(&current, &next, now);
        ttak_bigint_free(&next, now);
    }

    out->steps = steps;

    // Record final metrics from the largest value seen.
    out->max_bits = ttak_bigint_get_bit_length(&max_seen);
    out->max_dec_digits = bigint_decimal_digits_estimate(out->max_bits);
    ttak_bigint_to_hex_hash(&max_seen, out->max_hash);
    ttak_bigint_format_prefix(&max_seen, out->max_prefix, sizeof(out->max_prefix));
    // Also ensure max_value (u64) is saturated, as it's used in some logging.
    if (out->overflow) {
        out->max_value = UINT64_MAX;
    }

    ttak_bigint_free(&max_seen, now);
    ttak_bigint_free(&current, now);
    history_big_destroy(&hist);
}

/* Lightweight walker that stays in u64 lane until overflow forces a bigint detour. */
static void run_aliquot_sequence(uint64_t seed, uint32_t max_steps, uint64_t time_budget_ms, bool allow_bigints, aliquot_outcome_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->seed = seed;
    out->max_value = seed;
    out->final_value = seed;
    out->max_bits = bit_length_u64(seed);
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

        uint64_t dbg_start_ms = monotonic_millis();
        uint64_t next = 0;
        bool ok = sum_proper_divisors_u64_checked(current, &next);
        uint64_t dbg_elapsed_ms = monotonic_millis() - dbg_start_ms;
        if (dbg_elapsed_ms > 1000) {
            printf("[ALIQUOT] slow sum_divisors on %" PRIu64 " took %" PRIu64 "ms\n", current, dbg_elapsed_ms);
        }

        ttak_atomic_inc64(&g_total_probes);

        if (!ok) { // Overflow
            out->overflow = true;
            steps++;
            if (!allow_bigints) {
                break;
            }

            uint64_t now = monotonic_millis();
            ttak_bigint_t big_current;
            ttak_bigint_init(&big_current, now);
            ttak_bigint_set_u64(&big_current, current, now);

            ttak_bigint_t big_next;
            ttak_bigint_init(&big_next, now);
            if (!sum_proper_divisors_big(&big_current, &big_next, now)) {
                ttak_bigint_free(&big_next, now);
                ttak_bigint_free(&big_current, now);
                break;
            }

            run_aliquot_sequence_big(&big_next, steps, max_steps, time_budget_ms, out, start_ms);

            ttak_bigint_free(&big_next, now);
            ttak_bigint_free(&big_current, now);
            break;
        }

        if (next > out->max_value) {
            out->max_value = next;
            uint32_t bits = bit_length_u64(next);
            if (bits > out->max_bits) out->max_bits = bits;
        }
        steps++;

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

static uint32_t bigint_decimal_digits_estimate(uint32_t bits) {
    if (bits == 0) return 1;
    double approx = (double)bits * 0.3010299956639812; // log10(2)
    uint32_t digits = (uint32_t)approx + 1;
    return digits;
}

static void capture_track_metrics(const aliquot_outcome_t *out, const aliquot_job_t *job, uint64_t budget_ms, track_record_t *rec) {
    if (!out || !rec) return;
    memset(rec, 0, sizeof(*rec));
    rec->seed = out->seed;
    rec->steps = out->steps;
    rec->wall_time_ms = out->wall_time_ms;
    rec->budget_ms = budget_ms;
    rec->scout_score = job ? job->scout_score : 0.0;
    rec->priority = job ? job->priority : 0;
    snprintf(rec->ended, sizeof(rec->ended), "%s", track_end_reason(out));
    format_track_end_detail(out, rec->ended_by, sizeof(rec->ended_by));

    if (out->overflow) {
        // For big sequences, the metrics were pre-calculated.
        rec->max_bits = out->max_bits;
        rec->max_dec_digits = out->max_dec_digits;
        snprintf(rec->max_hash, sizeof(rec->max_hash), "%s", out->max_hash);
        snprintf(rec->max_prefix, sizeof(rec->max_prefix), "%s", out->max_prefix);
        rec->max_u64 = UINT64_MAX; // Signal that max_u64 is not the true max
    } else {
        // For u64 sequences, calculate metrics now.
        rec->max_u64 = out->max_value;
        uint64_t now = monotonic_millis();
        ttak_bigint_t max_bi;
        ttak_bigint_init(&max_bi, now);
        if (ttak_bigint_set_u64(&max_bi, out->max_value, now)) {
            rec->max_bits = ttak_bigint_get_bit_length(&max_bi);
            rec->max_dec_digits = bigint_decimal_digits_estimate(rec->max_bits);
            ttak_bigint_to_hex_hash(&max_bi, rec->max_hash);
            ttak_bigint_format_prefix(&max_bi, rec->max_prefix, sizeof(rec->max_prefix));
        } else {
            rec->max_bits = 0;
            rec->max_dec_digits = 0;
            rec->max_hash[0] = '\0';
            rec->max_prefix[0] = '\0';
        }
        ttak_bigint_free(&max_bi, now);
    }
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
    FILE *fp = fopen(g_found_log_path, "a");
    if (!fp) {
        fprintf(stderr, "[ALIQUOT] Failed to write %s: %s\n", g_found_log_path, strerror(errno));
        return;
    }
    for (size_t i = g_persisted_found_count; i < g_found_count; ++i) {
        const found_record_t *rec = &g_found_records[i];
        fprintf(fp, "{\"seed\":%" PRIu64 ",\"steps\":%" PRIu64 ",\"max\":%" PRIu64 ",\"final\":%" PRIu64 ",\"cycle\":%u,\"status\":\"%s\",\"source\":\"%s\"}\n",
                rec->seed, rec->steps, rec->max_value, rec->final_value,
                rec->cycle_length, rec->status, rec->provenance);
    }
    fclose(fp);
    g_persisted_found_count = g_found_count;
}

static void persist_jump_records(void) {
    FILE *fp = fopen(g_jump_log_path, "a");
    if (!fp) {
        fprintf(stderr, "[ALIQUOT] Failed to write %s: %s\n", g_jump_log_path, strerror(errno));
        return;
    }
    for (size_t i = g_persisted_jump_count; i < g_jump_count; ++i) {
        const jump_record_t *rec = &g_jump_records[i];
        fprintf(fp, "{\"seed\":%" PRIu64 ",\"steps\":%" PRIu64 ",\"max\":%" PRIu64 ",\"score\":%.2f,\"overflow\":%.3f}\n",
                rec->seed, rec->preview_steps, rec->preview_max, rec->score, rec->overflow_pressure);
    }
    fclose(fp);
    g_persisted_jump_count = g_jump_count;
}

static void persist_track_records(void) {
    FILE *fp = fopen(g_track_log_path, "a");
    if (!fp) {
        fprintf(stderr, "[ALIQUOT] Failed to write %s: %s\n", g_track_log_path, strerror(errno));
        return;
    }
    for (size_t i = g_persisted_track_count; i < g_track_count; ++i) {
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
    g_persisted_track_count = g_track_count;
}

/* The JSON snapshot is intentionally simple so operators can inspect it by hand. */
static void persist_queue_state(void) {
    uint64_t pending[JOB_QUEUE_CAP];
    size_t count = pending_queue_snapshot(pending, JOB_QUEUE_CAP);

    FILE *fp = fopen(g_queue_state_path, "w");
    if (!fp) {
        fprintf(stderr, "[ALIQUOT] Failed to write %s: %s\n", g_queue_state_path, strerror(errno));
        return;
    }
    fprintf(fp, "{\"pending\":[");
    for (size_t i = 0; i < count; ++i) {
        fprintf(fp, "%s%" PRIu64, (i == 0) ? "" : ",", pending[i]);
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
    g_persisted_found_count = g_found_count;
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
    g_persisted_jump_count = g_jump_count;
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
    g_persisted_track_count = g_track_count;
}

static bool enqueue_job(aliquot_job_t *job, const char *source_tag) {
    if (!job || !g_thread_pool) {
        return false;
    }
    /* The pending queue mirrors the work handed to the pool so disk snapshots stay honest. */
    if (!pending_queue_add(job->seed)) {
        fprintf(stderr, "[ALIQUOT] Job queue saturated; dropping seed %" PRIu64 " (%s)\n",
                job->seed, source_tag ? source_tag : "unknown");
        return false;
    }
    uint64_t now = monotonic_millis();
    ttak_future_t *future = ttak_thread_pool_submit_task(
        g_thread_pool, worker_process_job_wrapper, job, job->priority, now);
    if (!future) {
        pending_queue_remove(job->seed);
        fprintf(stderr, "[ALIQUOT] Thread pool rejected seed %" PRIu64 " (%s)\n",
                job->seed, source_tag ? source_tag : "unknown");
        return false;
    }
    (void)future;
    return true;
}

/* Recreate outstanding work that was captured during the previous persistence pass. */
static void load_queue_checkpoint(void) {
    if (!g_thread_pool) return;
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
                uint64_t alloc_now = monotonic_millis();
                aliquot_job_t *job_ptr = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, alloc_now);
                if (!job_ptr) {
                    fprintf(stderr, "[ALIQUOT] Failed to allocate memory for checkpoint job for seed %" PRIu64 "\n", seed);
                    continue;
                }
                memset(job_ptr, 0, sizeof(aliquot_job_t));
                job_ptr->seed = seed;
                job_ptr->priority = 1;
                job_ptr->scout_score = 0.0;
                snprintf(job_ptr->provenance, sizeof(job_ptr->provenance), "%s", "checkpoint");
                if (!enqueue_job(job_ptr, "checkpoint")) {
                    ttak_mem_free(job_ptr);
                }
            }
        }
    }
    ttak_mem_free(buffer);
}

static void *worker_process_job_wrapper(void *arg) {
    aliquot_job_t *job = (aliquot_job_t *)arg;
    pending_queue_remove(job->seed);
    process_job(job);
    ttak_mem_free(job); // Free the job allocated by scout_main
    return NULL;
}

static void process_job(const aliquot_job_t *job) {
    if (!job) return;
    aliquot_outcome_t outcome;
    uint64_t budget_ms = determine_time_budget(job);
    run_aliquot_sequence(job->seed, LONG_RUN_MAX_STEPS, budget_ms, true, &outcome);
    append_found_record(&outcome, job->provenance);
    append_track_record(&outcome, job, budget_ms);
    maybe_flush_ledgers();
}

/* Background sampler that looks for promising seeds and promotes them to the pool. */
static void *scout_main(void *arg) {
    (void)arg;
    while (!atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) {
        size_t depth = pending_queue_depth();
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
        run_aliquot_sequence(seed, SCOUT_PREVIEW_STEPS, 0, false, &probe);
        double score = 0.0;
        double overflow_pressure = compute_overflow_pressure(&probe);
        if (looks_long(&probe, &score)) {
            append_jump_record(seed, probe.steps, probe.max_value, score, overflow_pressure);
            
            uint64_t now = monotonic_millis();
            aliquot_job_t *job_ptr = ttak_mem_alloc(sizeof(aliquot_job_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
            if (!job_ptr) {
                fprintf(stderr, "[SCOUT] Failed to allocate job for seed %" PRIu64 "\n", seed);
                responsive_sleep(SCOUT_SLEEP_MS);
                continue;
            }
            memset(job_ptr, 0, sizeof(aliquot_job_t));
            job_ptr->seed = seed;
            job_ptr->priority = (probe.overflow || overflow_pressure >= 45.0) ? 3 : 2;
            job_ptr->preview_steps = probe.steps;
            job_ptr->preview_max = probe.max_value;
            job_ptr->preview_overflow = probe.overflow || (overflow_pressure >= 45.0);
            job_ptr->scout_score = score;
            snprintf(job_ptr->provenance, sizeof(job_ptr->provenance), "%s", "scout");
            if (!enqueue_job(job_ptr, "scout")) {
                ttak_mem_free(job_ptr);
            } else {
                maybe_flush_ledgers();
            }
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
    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    ttak_atomic_write64(&g_last_persist_ms, monotonic_millis());

    load_found_records();
    load_jump_records();
    load_track_records();

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1) cpus = 1;
    if (cpus > MAX_WORKERS) cpus = MAX_WORKERS;
    
    uint64_t now = monotonic_millis();
    g_thread_pool = ttak_thread_pool_create((size_t)cpus, 0, now);
    if (!g_thread_pool) {
        fprintf(stderr, "[ALIQUOT] Failed to create thread pool.\n");
        return 1;
    }

    load_queue_checkpoint();

    pthread_t scout_thread;
    pthread_create(&scout_thread, NULL, scout_main, NULL);

    while (!atomic_load_explicit(&shutdown_requested, memory_order_relaxed)) {
        responsive_sleep(1000);
        maybe_flush_ledgers();
        size_t queue_depth = pending_queue_depth();
        uint64_t completed = ttak_atomic_read64(&g_total_sequences);
        printf("[ALIQUOT] queue=%zu completed=%" PRIu64 " probes=%" PRIu64 "\n",
               queue_depth, completed, ttak_atomic_read64(&g_total_probes));
    }

    printf("[ALIQUOT] Shutdown requested. Waiting for threads to exit...\n");
    fflush(stdout);

    pthread_join(scout_thread, NULL);
    ttak_thread_pool_destroy(g_thread_pool);

    flush_ledgers();
    printf("[ALIQUOT] Shutdown complete.\n");
    return 0;
}
