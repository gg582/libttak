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
#include <pthread.h>
#include <sys/stat.h>
#include <curl/curl.h>

#include <ttak/mem/mem.h>
#include <ttak/math/ntt.h>
#include <ttak/timing/timing.h>
#include <ttak/atomic/atomic.h>
#include "../internal/app_types.h"
#include "hwinfo.h"

#define CHECKPOINT_FILE "/home/yjlee/Documents/mersenne_checkpoint.json"
#define MAX_WORKERS 8

static volatile atomic_bool shutdown_requested = false;
static atomic_uint g_next_p;
static ttak_hw_spec_t g_hw_spec;
static app_state_t *g_app_state = NULL;
static uint64_t g_start_tick;

typedef struct {
    alignas(64) atomic_uint_fast64_t ops_count;
    uint32_t id;
} worker_ctx_t;

static worker_ctx_t g_workers[MAX_WORKERS];
static int g_num_workers = 4;

/* External gateway linkage */
extern void save_current_progress(const char *filename, const void *data, size_t size);
extern int report_to_gimps(app_state_t *state, const gimps_result_t *res, const ttak_node_telemetry_t *telemetry);
extern void generate_computer_id(char *buf, size_t len);

static void handle_signal(int sig) {
    (void)sig;
    atomic_store(&shutdown_requested, true);
}

/**
 * @brief Performs Lucas-Lehmer test core.
 * @return 1 if prime, 0 if composite, -1 if aborted.
 */
static int ttak_ll_test_core(uint32_t p, uint64_t *out_residue) {
    if (p == 2) {
        *out_residue = 0;
        return 1;
    }

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

/**
 * @brief Worker thread for exponent testing and reporting.
 */
void* worker_thread(void* arg) {
    worker_ctx_t *ctx = (worker_ctx_t*)arg;
    while (!atomic_load(&shutdown_requested)) {
        uint32_t p = atomic_fetch_add(&g_next_p, 2);

        bool p_is_prime = true;
        if (p < 2) p_is_prime = false;
        else if (p % 2 == 0) p_is_prime = (p == 2);
        else {
            for (uint32_t i = 3; i * i <= p; i += 2) {
                if (p % i == 0) { p_is_prime = false; break; }
            }
        }
        if (!p_is_prime) continue;

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
        }
        atomic_fetch_add_explicit(&ctx->ops_count, 1, memory_order_relaxed);
    }
    return NULL;
}

int main(int argc, char **argv) {
    /* Critical: Initialize libcurl globally for thread safety */
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

    uint32_t resume_p = 3;
    FILE *f = fopen(CHECKPOINT_FILE, "r");
    if (f) {
        char ln[256];
        while (fgets(ln, sizeof(ln), f)) {
            char *ptr = strstr(ln, "\"last_p\"");
            if (ptr && (ptr = strchr(ptr, ':'))) resume_p = (uint32_t)atoi(ptr + 1);
        }
        fclose(f);
    }
    atomic_store(&g_next_p, (resume_p % 2 == 0) ? resume_p + 1 : resume_p + 2);

    pthread_t th[MAX_WORKERS];
    for (int i = 0; i < g_num_workers; i++) {
        g_workers[i].id = i;
        atomic_init(&g_workers[i].ops_count, 0);
        pthread_create(&th[i], NULL, worker_thread, &g_workers[i]);
    }

    while (!atomic_load(&shutdown_requested)) {
        usleep(1000000);
        uint64_t total_ops = 0;
        for (int i = 0; i < g_num_workers; i++) {
            total_ops += atomic_load_explicit(&g_workers[i].ops_count, memory_order_relaxed);
        }

        uint32_t cp = atomic_load(&g_next_p);
        printf("[SYSTEM] Exponent: %u | Total Ops: %lu\n", cp, total_ops);
        fflush(stdout);

        char json[256];
        int len = snprintf(json, sizeof(json), "{\n    \"last_p\": %u,\n    \"total_ops\": %lu\n}\n", cp, total_ops);
        if (len > 0) save_current_progress(CHECKPOINT_FILE, json, (size_t)len);
    }

    for (int i = 0; i < g_num_workers; i++) pthread_join(th[i], NULL);
    
    ttak_mem_free(g_app_state);
    curl_global_cleanup();
    return 0;
}
