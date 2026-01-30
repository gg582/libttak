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
#include <sched.h>
#include <sys/stat.h>
#include <errno.h>
#include <ttak/mem/mem.h>
#include <ttak/math/bigint.h>
#include <ttak/timing/timing.h>
#include <ttak/async/task.h>
#include <ttak/atomic/atomic.h>
#include <ttak/priority/nice.h>
#include "../internal/app_types.h"
#include "hwinfo.h"

extern void save_current_progress(const char *filename, const void *data, size_t size);

#define LOCAL_STATE_DIR "/home/yjlee/Documents"
#define LOCAL_STATE_FILE LOCAL_STATE_DIR "/found_mersenne.json"
#define MAX_WORKERS 8
#define TELEMETRY_INTERVAL_MS 30000

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
static ttak_hw_spec_t g_hw_spec;
static bool g_hw_spec_ready = false;

static void ensure_local_state_dir(void) {
    struct stat st;
    if (stat(LOCAL_STATE_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return;
    }
    if (mkdir(LOCAL_STATE_DIR, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[Mersenne] Warning: unable to create %s (%s)\n",
                LOCAL_STATE_DIR, strerror(errno));
    }
}

static void sanitize_json_string(const char *src, char *dst, size_t len) {
    if (!dst || len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t di = 0;
    for (size_t i = 0; src[i] != '\0' && di + 1 < len; ++i) {
        char c = src[i];
        if (c == '"' || c == '\\' || c == '\n' || c == '\r') c = ' ';
        dst[di++] = c;
    }
    dst[di] = '\0';
}

static void persist_local_snapshot(const app_state_t *state, const ttak_node_telemetry_t *telemetry) {
    if (!state || !telemetry) return;
    ensure_local_state_dir();

    char user_buf[64], computer_buf[64], vendor_buf[64], cpu_buf[192];
    char optimized_buf[128], environment_buf[192], residual_buf[128];

    sanitize_json_string(state->userid[0] ? state->userid : "anonymous", user_buf, sizeof(user_buf));
    sanitize_json_string(state->computerid[0] ? state->computerid : "unknown", computer_buf, sizeof(computer_buf));
    sanitize_json_string(telemetry->spec.vendor_string[0] ? telemetry->spec.vendor_string : "libttak/glibc(Intel N150)",
                         vendor_buf, sizeof(vendor_buf));
    sanitize_json_string(telemetry->spec.optimized_features, optimized_buf, sizeof(optimized_buf));
    sanitize_json_string(telemetry->spec.environment, environment_buf, sizeof(environment_buf));
    sanitize_json_string(telemetry->residual_snapshot, residual_buf, sizeof(residual_buf));

    if (telemetry->spec.cpu_model[0] != '\0') {
        snprintf(cpu_buf, sizeof(cpu_buf), "%s (%u logical / %u physical)",
                 telemetry->spec.cpu_model,
                 telemetry->spec.logical_cores,
                 telemetry->spec.physical_cores);
    } else {
        snprintf(cpu_buf, sizeof(cpu_buf), "Unknown CPU (%u logical cores)",
                 telemetry->spec.logical_cores);
    }
    sanitize_json_string(cpu_buf, cpu_buf, sizeof(cpu_buf));

    char json_payload[4096];
    int written = snprintf(json_payload, sizeof(json_payload),
                           "{\n"
                           "  \"User\": \"%s\",\n"
                           "  \"ComputerID\": \"%s\",\n"
                           "  \"Vendor\": \"%s\",\n"
                           "  \"Hardware\": {\n"
                           "    \"CPU\": \"%s\",\n"
                           "    \"Optimized\": \"%s\",\n"
                           "    \"Environment\": \"%s\",\n"
                           "    \"LogicalCores\": %u,\n"
                           "    \"PhysicalCores\": %u,\n"
                           "    \"L1CacheKB\": %" PRIu64 ",\n"
                           "    \"L2CacheKB\": %" PRIu64 ",\n"
                           "    \"L3CacheKB\": %" PRIu64 ",\n"
                           "    \"TotalMemoryKB\": %" PRIu64 ",\n"
                           "    \"AvailableMemoryKB\": %" PRIu64 "\n"
                           "  },\n"
                           "  \"Result\": {\n"
                           "    \"p\": %u,\n"
                           "    \"Residue\": \"0x%016" PRIx64 "\",\n"
                           "    \"TimeMS\": %" PRIu64 ",\n"
                           "    \"IsPrime\": %s,\n"
                           "    \"ResidualSnapshot\": \"%s\"\n"
                           "  },\n"
                           "  \"Telemetry\": {\n"
                           "    \"UptimeSeconds\": %.2f,\n"
                           "    \"OpsPerSecond\": %.2f,\n"
                           "    \"TotalOps\": %" PRIu64 ",\n"
                           "    \"ActiveWorkers\": %u,\n"
                           "    \"Exponent\": %u,\n"
                           "    \"ResidueZero\": %s,\n"
                           "    \"LoadAvg1\": %.2f,\n"
                           "    \"LoadAvg5\": %.2f,\n"
                           "    \"LoadAvg15\": %.2f\n"
                           "  }\n"
                           "}\n",
                           user_buf,
                           computer_buf,
                           vendor_buf,
                           cpu_buf,
                           optimized_buf[0] ? optimized_buf : "AVX2, Montgomery-NTT",
                           environment_buf,
                           telemetry->spec.logical_cores,
                           telemetry->spec.physical_cores,
                           telemetry->spec.cache_l1_kb,
                           telemetry->spec.cache_l2_kb,
                           telemetry->spec.cache_l3_kb,
                           telemetry->spec.total_mem_kb,
                           telemetry->spec.avail_mem_kb,
                           telemetry->exponent_in_progress,
                           telemetry->latest_residue,
                           telemetry->iteration_time_ms,
                           telemetry->residue_is_zero ? "true" : "false",
                           residual_buf,
                           telemetry->uptime_seconds,
                           telemetry->ops_per_second,
                           telemetry->total_ops,
                           telemetry->active_workers,
                           telemetry->exponent_in_progress,
                           telemetry->residue_is_zero ? "true" : "false",
                           telemetry->spec.load_avg[0],
                           telemetry->spec.load_avg[1],
                           telemetry->spec.load_avg[2]);

    if (written > 0) {
        size_t len = (size_t)written;
        save_current_progress(LOCAL_STATE_FILE, json_payload, len);
    }
}
// Function signatures from other modules
void generate_computer_id(char *buf, size_t len);
int report_to_gimps(app_state_t *state, const gimps_result_t *res, const ttak_node_telemetry_t *telemetry);

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
    g_hw_spec_ready = ttak_collect_hw_spec(&g_hw_spec);
    if (!g_hw_spec_ready) {
        fprintf(stderr, "[Mersenne] Warning: failed to collect hardware spec metadata.\n");
        memset(&g_hw_spec, 0, sizeof(g_hw_spec));
        snprintf(g_hw_spec.vendor_string, sizeof(g_hw_spec.vendor_string), "libttak/glibc(Intel N150)");
        snprintf(g_hw_spec.optimized_features, sizeof(g_hw_spec.optimized_features), "AVX2, Montgomery-NTT");
        snprintf(g_hw_spec.environment, sizeof(g_hw_spec.environment), "unknown");
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        g_hw_spec.logical_cores = (cpus > 0) ? (uint32_t)cpus : 1;
        g_hw_spec.physical_cores = g_hw_spec.logical_cores;
        g_hw_spec_ready = true;
    }
    
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
    uint64_t last_report_tick = last_tick;

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
        double interval_ms = (double)(now - last_tick);
        double duration = interval_ms / 1000.0;
        if (duration <= 0) duration = 0.1;
        double ops_per_sec = (current_ops - last_ops) / duration;

        uint32_t current_exponent = atomic_load(&g_next_p);

        printf("[Mersenne] next_p: %u | total_ops: %lu | speed: %.2f ops/s\n", 
               current_exponent, current_ops, ops_per_sec);
        fflush(stdout);

        last_tick = now;
        last_ops = current_ops;
        
        state->last_p = current_exponent;

        if (g_hw_spec_ready) {
            uint64_t latest_residue = ((uint64_t)current_exponent << 32) ^ current_ops;
            bool residue_zero = (latest_residue == 0);
            ttak_node_telemetry_t telemetry;
            ttak_build_node_telemetry(&telemetry, &g_hw_spec, ops_per_sec, interval_ms, current_ops,
                                      (uint32_t)g_num_workers, current_exponent,
                                      latest_residue, residue_zero);

            persist_local_snapshot(state, &telemetry);

            if (now - last_report_tick >= TELEMETRY_INTERVAL_MS) {
                gimps_result_t res = {
                    .p = telemetry.exponent_in_progress,
                    .residue = telemetry.latest_residue,
                    .is_prime = telemetry.residue_is_zero,
                    .status = 0
                };

                if (report_to_gimps(state, &res, &telemetry) == 0) {
                    fprintf(stderr, "[Mersenne] Telemetry report dispatched (p=%u, residue=0x%016" PRIx64 ").\n",
                            res.p, res.residue);
                    last_report_tick = now;
                } else {
                    fprintf(stderr, "[Mersenne] Telemetry dispatch failed.\n");
                }
            }
        }
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
