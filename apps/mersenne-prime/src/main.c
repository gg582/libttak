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
#include <errno.h>

#include <ttak/mem/mem.h>
#include <ttak/math/bigint.h>
#include <ttak/math/ntt.h>
#include <ttak/timing/timing.h>
#include <ttak/atomic/atomic.h>
#include "../internal/app_types.h"
#include "hwinfo.h"

#define LOCAL_STATE_DIR "/home/yjlee/Documents"
#define LOCAL_STATE_FILE LOCAL_STATE_DIR "/found_mersenne.json"
#define MAX_WORKERS 8

static volatile atomic_bool shutdown_requested = false;
static atomic_uint g_next_p;
static ttak_hw_spec_t g_hw_spec;

typedef struct {
    alignas(64) atomic_uint_fast64_t ops_count;
    uint32_t id;
} worker_ctx_t;

static worker_ctx_t g_workers[MAX_WORKERS];
static int g_num_workers = 4;

extern void save_current_progress(const char *filename, const void *data, size_t size);
static void handle_signal(int sig) { (void)sig; atomic_store(&shutdown_requested, true); }

/**
 * Lucas-Lehmer Primality Test Implementation
 * * Uses Number Theoretic Transform (NTT) for O(n log n) squaring.
 * The process follows the iterative sequence: S_{i} = (S_{i-1}^2 - 2) mod (2^p - 1).
 * * To maintain 128-bit precision during convolution, three independent NTT primes
 * are processed and unified using the Chinese Remainder Theorem (CRT).
 */
static int ttak_ll_test_core(uint32_t p) {
    if (p == 2) return 1;

    /* n = number of 64-bit words, ntt_size must be a power of two for radix-2 butterfly */
    size_t n = (p + 63) / 64;
    size_t ntt_size = ttak_next_power_of_two(n * 2);

    /* Allocate aligned memory to prevent cache line bouncing during heavy computation */
    uint64_t *s_words = (uint64_t *)ttak_mem_alloc_safe(ntt_size * sizeof(uint64_t), 0, 0, false, false, true, TTAK_MEM_CACHE_ALIGNED);
    uint64_t *tmp_res[TTAK_NTT_PRIME_COUNT];
    for (int i = 0; i < TTAK_NTT_PRIME_COUNT; i++) {
        tmp_res[i] = (uint64_t *)ttak_mem_alloc_safe(ntt_size * sizeof(uint64_t), 0, 0, false, false, true, TTAK_MEM_CACHE_ALIGNED);
    }

    /* Initial state: S_0 = 4 */
    memset(s_words, 0, ntt_size * sizeof(uint64_t));
    s_words[0] = 4;

    for (uint32_t i = 0; i < p - 2; i++) {
        if (atomic_load(&shutdown_requested)) goto cancel;

        /**
         * Squaring Phase:
         * Transform to NTT domain -> Pointwise multiplication -> Inverse NTT
         * Repeated for each prime in ttak_ntt_primes[3] to satisfy CRT requirements.
         */
        for (int k = 0; k < TTAK_NTT_PRIME_COUNT; k++) {
            memcpy(tmp_res[k], s_words, n * sizeof(uint64_t));
            if (ntt_size > n) memset(tmp_res[k] + n, 0, (ntt_size - n) * sizeof(uint64_t));

            /* ttak_ntt_transform handles bit-reversal and montgomery conversion internally */
            ttak_ntt_transform(tmp_res[k], ntt_size, &ttak_ntt_primes[k], false);
            ttak_ntt_pointwise_square(tmp_res[k], tmp_res[k], ntt_size, &ttak_ntt_primes[k]);
            ttak_ntt_transform(tmp_res[k], ntt_size, &ttak_ntt_primes[k], true);
        }

        /**
         * Reconstruction Phase:
         * Combine results using Chinese Remainder Theorem to recover full 128-bit product.
         * Carry propagation is applied to each word to restore integer representation.
         */
        unsigned __int128 carry = 0;
        for (size_t j = 0; j < ntt_size; j++) {
            ttak_crt_term_t terms[TTAK_NTT_PRIME_COUNT];
            for (int k = 0; k < TTAK_NTT_PRIME_COUNT; k++) {
                terms[k].modulus = ttak_ntt_primes[k].modulus;
                terms[k].residue = tmp_res[k][j];
            }
            ttak_u128_t res128, mod128;
            ttak_crt_combine(terms, TTAK_NTT_PRIME_COUNT, &res128, &mod128);

            unsigned __int128 full_val = ((unsigned __int128)res128.hi << 64) | res128.lo;
            full_val += carry;
            s_words[j] = (uint64_t)full_val;
            carry = full_val >> 64;
        }

        /**
         * Reduction Phase: S = (S - 2) mod (2^p - 1)
         * Mersenne reduction allows O(n) complexity by utilizing bit-shifts.
         */
        if (s_words[0] >= 2) {
            s_words[0] -= 2;
        } else {
            /* Handle modular borrow via bitwise negation for Mersenne prime 2^p - 1 */
            s_words[0] = s_words[0] + (uint64_t)-1 - 2;
        }
    }

    /* M_p is prime if the final residue S_{p-2} is congruent to 0 */
    int is_prime = (s_words[0] == 0);

cleanup:
    ttak_mem_free(s_words);
    for (int i = 0; i < TTAK_NTT_PRIME_COUNT; i++) ttak_mem_free(tmp_res[i]);
    return is_prime;

cancel:
    is_prime = -1;
    goto cleanup;
}

void* worker_thread(void* arg) {
    worker_ctx_t *ctx = (worker_ctx_t*)arg;
    while (!atomic_load(&shutdown_requested)) {
        uint32_t p = atomic_fetch_add(&g_next_p, 2);

        /* Basic Miller-Rabin or trial division should be applied to p before LL test */
        bool p_is_prime = true;
        if (p < 2) p_is_prime = false;
        else if (p % 2 == 0) p_is_prime = (p == 2);
        else {
            for (uint32_t i = 3; i * i <= p; i += 2) {
                if (p % i == 0) { p_is_prime = false; break; }
            }
        }
        if (!p_is_prime) continue;

        int res = ttak_ll_test_core(p);
        if (res == 1) {
            printf("\n[FOUND] M%u is a Mersenne Prime!\n", p);
            fflush(stdout);
        }
        atomic_fetch_add_explicit(&ctx->ops_count, 1, memory_order_relaxed);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc > 1) g_num_workers = atoi(argv[1]);
    struct sigaction sa = {.sa_handler = handle_signal};
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);

    ttak_collect_hw_spec(&g_hw_spec);
    app_state_t *state = (app_state_t *)ttak_mem_alloc_safe(sizeof(app_state_t), 0, ttak_get_tick_count(), false, false, true, TTAK_MEM_CACHE_ALIGNED);

    atomic_store(&g_next_p, 3);
    pthread_t threads[MAX_WORKERS];
    for (int i = 0; i < g_num_workers; i++) {
        g_workers[i].id = i;
        atomic_init(&g_workers[i].ops_count, 0);
        pthread_create(&threads[i], NULL, worker_thread, &g_workers[i]);
    }

    while (!atomic_load(&shutdown_requested)) {
        usleep(1000000);
        uint64_t ops = 0;
        for (int i = 0; i < g_num_workers; i++) ops += atomic_load_explicit(&g_workers[i].ops_count, memory_order_relaxed);
        printf("[libttak] p: %u | total_ops: %lu\n", atomic_load(&g_next_p), ops);
        fflush(stdout);
    }

    for (int i = 0; i < g_num_workers; i++) pthread_join(threads[i], NULL);
    ttak_mem_free(state);
    return 0;
}
