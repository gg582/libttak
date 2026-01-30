#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
#include <ttak/mem/mem.h>
#include <ttak/math/bigint.h>
#include <ttak/timing/timing.h>
#include <ttak/async/task.h>
#include <ttak/atomic/atomic.h>
#include <ttak/priority/nice.h>
#include "../internal/app_types.h"

#define STATE_FILE "found_mersenne.json"
#define MAX_WORKERS 8

// Global shutdown flag
static volatile atomic_bool shutdown_requested = false;

// Per-worker statistics to avoid cache line bouncing
typedef struct {
    alignas(64) atomic_uint_fast64_t ops_count;
    uint32_t id;
} worker_ctx_t;

static worker_ctx_t g_workers[MAX_WORKERS];
static int g_num_workers = 4;
static atomic_uint g_next_p;

// Function signatures from other modules
void generate_computer_id(char *buf, size_t len);
int report_to_gimps(app_state_t *state, gimps_result_t *res);
extern void save_current_progress(const char *filename, const void *data, size_t size);

static void handle_signal(int sig) {
    (void)sig;
    atomic_store(&shutdown_requested, true);
}

/**
 * @brief Simulated Lucas-Lehmer computation.
 */
void compute_mersenne_benchmark(uint32_t p) {
    (void)p;
    // In a real scenario, this would be intensive.
    // For benchmarking, we just do a small bit of work.
    volatile uint64_t sum = 0;
    for (int i = 0; i < 1000; i++) sum += i;
    (void)sum;
}

/**
 * @brief Worker thread function.
 */
void* worker_thread(void* arg) {
    worker_ctx_t *ctx = (worker_ctx_t*)arg;

    // Block signals in worker threads
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    while (!atomic_load(&shutdown_requested)) {
        uint32_t p = atomic_fetch_add(&g_next_p, 2);
        
        compute_mersenne_benchmark(p);
        
        atomic_fetch_add_explicit(&ctx->ops_count, 1, memory_order_relaxed);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        g_num_workers = atoi(argv[1]);
        if (g_num_workers > MAX_WORKERS) g_num_workers = MAX_WORKERS;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    uint64_t start_time = ttak_get_tick_count();
    
    app_state_t *state = ttak_mem_alloc_safe(sizeof(app_state_t), __TTAK_UNSAFE_MEM_FOREVER__, 
                                           start_time, false, false, true, TTAK_MEM_CACHE_ALIGNED);
    // Load state logic omitted for brevity in benchmark mode, but kept for structure
    generate_computer_id(state->computerid, sizeof(state->computerid));
    state->last_p = 3;
    atomic_store(&g_next_p, state->last_p);

    pthread_t threads[MAX_WORKERS];
    for (int i = 0; i < g_num_workers; i++) {
        g_workers[i].id = i;
        atomic_init(&g_workers[i].ops_count, 0);
        pthread_create(&threads[i], NULL, worker_thread, &g_workers[i]);
    }

    uint64_t last_tick = ttak_get_tick_count();
    uint64_t last_ops = 0;

    while (!atomic_load(&shutdown_requested)) {
        // Sleep in smaller increments for better responsiveness to signals
        for (int i = 0; i < 10 && !atomic_load(&shutdown_requested); i++) {
            usleep(100000); // 100ms
        }
        
        if (atomic_load(&shutdown_requested)) break;

        uint64_t current_ops = 0;
        for (int i = 0; i < g_num_workers; i++) {
            current_ops += atomic_load_explicit(&g_workers[i].ops_count, memory_order_relaxed);
        }

        uint64_t now = ttak_get_tick_count();
        double duration = (now - last_tick) / 1000.0;
        if (duration <= 0) duration = 0.1;
        double ops_per_sec = (current_ops - last_ops) / duration;

        printf("[Mersenne] next_p: %u | total_ops: %lu | speed: %.2f ops/s\n", 
               atomic_load(&g_next_p), current_ops, ops_per_sec);
        fflush(stdout);

        last_tick = now;
        last_ops = current_ops;
        
        state->last_p = atomic_load(&g_next_p);
    }

    // Ensure we don't hang on join if something goes wrong
    fprintf(stderr, "Shutting down workers...\n");
    for (int i = 0; i < g_num_workers; i++) {
        pthread_join(threads[i], NULL);
    }

    fprintf(stderr, "Cleaning up...\n");
    ttak_mem_free(state);

    return 0;
}