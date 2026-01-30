#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>
#include "thread_compat.h"
#include "lockfree_queue.h"

// Task and Status Enums
typedef enum {
    TASK_STATE_IDLE,
    TASK_STATE_RUNNING,
    TASK_STATE_DONE,
    TASK_STATE_CANCELLED
} task_state_t;

typedef enum {
    STATUS_UNKNOWN,
    STATUS_PRIME,
    STATUS_COMPOSITE,
    STATUS_ERROR
} mersenne_status_t;

// Task Structure
typedef struct {
    int p;
    task_state_t state;
    uint64_t iterations_done;
    uint64_t elapsed_ms;
    bool residue_is_zero;
    int error_code;
    mersenne_status_t status;
} mersenne_task_t;

// Global Control
static atomic_bool g_shutdown_requested = false;
static atomic_int g_highest_p_started = 0;
static atomic_int g_highest_p_finished = 0;
static atomic_uint_least64_t g_total_ops = 0;

// Result Queue (MPSC)
static ttak_lf_queue_t g_result_q;
static pthread_mutex_t g_result_q_lock = PTHREAD_MUTEX_INITIALIZER;

// Signal handler
void handle_sigint(int sig) {
    (void)sig;
    atomic_store(&g_shutdown_requested, true);
}

// Minimal BigInt logic for LL test (p <= 127)
typedef unsigned __int128 uint128;

static inline uint128 llt_sqr_mod(uint128 s, int p) {
    uint64_t s0 = (uint64_t)s;
    uint64_t s1 = (uint64_t)(s >> 64);
    
    unsigned __int128 p0 = (unsigned __int128)s0 * s0;
    unsigned __int128 p1 = (unsigned __int128)s0 * s1;
    unsigned __int128 p2 = (unsigned __int128)s1 * s1;
    
    uint64_t d[4];
    d[0] = (uint64_t)p0;
    unsigned __int128 m1 = (p0 >> 64) + (uint64_t)p1 + (uint64_t)p1;
    d[1] = (uint64_t)m1;
    unsigned __int128 m2 = (m1 >> 64) + (p1 >> 64) + (p1 >> 64) + (uint64_t)p2;
    d[2] = (uint64_t)m2;
    d[3] = (uint64_t)(p2 >> 64) + (uint64_t)(m2 >> 64);
    
    uint128 L = (((uint128)d[1] << 64) | d[0]);
    uint128 M = ((uint128)1 << p) - 1;
    if (p < 128) L &= M;

    uint64_t h[4] = {0};
    int limb_shift = p / 64;
    int bit_shift = p % 64;
    if (bit_shift == 0) {
        for (int i = 0; i < 4 - limb_shift; i++) h[i] = d[i + limb_shift];
    } else {
        for (int i = 0; i < 3 - limb_shift; i++) {
            h[i] = (d[i + limb_shift] >> bit_shift) | (d[i + limb_shift + 1] << (64 - bit_shift));
        }
        h[3 - limb_shift] = d[3] >> bit_shift;
    }
    uint128 H = ((uint128)h[1] << 64) | h[0];
    
    uint128 res = L + H;
    while (res >= M) res -= M;
    return res;
}

void lucas_lehmer_test(mersenne_task_t *task) {
    if (task->p == 2) {
        task->status = STATUS_PRIME;
        task->residue_is_zero = true;
        task->state = TASK_STATE_DONE;
        return;
    }
    uint128 s = 4;
    uint128 M = ((uint128)1 << task->p) - 1;
    uint64_t start = ttak_get_tick_count();
    uint64_t iters = 0;
    for (int i = 0; i < task->p - 2; i++) {
        if (atomic_load(&g_shutdown_requested)) {
            task->state = TASK_STATE_CANCELLED;
            task->iterations_done = iters;
            return;
        }
        s = llt_sqr_mod(s, task->p);
        if (s < 2) s = M - (2 - s); else s -= 2;
        iters++;
    }
    task->iterations_done = iters;
    task->residue_is_zero = (s == 0);
    task->status = task->residue_is_zero ? STATUS_PRIME : STATUS_COMPOSITE;
    task->elapsed_ms = ttak_get_tick_count() - start;
    task->state = TASK_STATE_DONE;
    atomic_fetch_add(&g_total_ops, iters);
}

bool is_prime_exponent(int n) {
    if (n < 2) return false;
    for (int i = 2; i * i <= n; i++) if (n % i == 0) return false;
    return true;
}

void* worker_loop(void* arg) {
    ttak_lf_queue_t *task_q = (ttak_lf_queue_t*)arg;
    while (!atomic_load(&g_shutdown_requested)) {
        mersenne_task_t *task = ttak_lf_queue_pop(task_q);
        if (task) {
            task->state = TASK_STATE_RUNNING;
            lucas_lehmer_test(task);
            pthread_mutex_lock(&g_result_q_lock);
            while (!ttak_lf_queue_push(&g_result_q, task)) {
                pthread_mutex_unlock(&g_result_q_lock);
                ttak_thread_yield();
                pthread_mutex_lock(&g_result_q_lock);
            }
            pthread_mutex_unlock(&g_result_q_lock);
        } else {
            usleep(1000);
        }
    }
    return NULL;
}

void* producer_loop(void* arg) {
    ttak_lf_queue_t *task_q = (ttak_lf_queue_t*)arg;
    int p = 2;
    while (!atomic_load(&g_shutdown_requested)) {
        if (is_prime_exponent(p)) {
            mersenne_task_t *task = calloc(1, sizeof(mersenne_task_t));
            task->p = p;
            task->state = TASK_STATE_IDLE;
            while (!ttak_lf_queue_push(task_q, task)) {
                if (atomic_load(&g_shutdown_requested)) { free(task); return NULL; }
                ttak_thread_yield();
            }
            atomic_store(&g_highest_p_started, p);
        }
        p++;
        if (p > 500) usleep(10000); // Slow down for demo
    }
    return NULL;
}

void save_state(mersenne_task_t **results, int count) {
    FILE *fp = fopen("found_mersenne.json.tmp", "w");
    if (!fp) return;
    fprintf(fp, "{\n  \"last_p_started\": %d,\n  \"last_p_finished\": %d,\n  \"results\": [\n",
            atomic_load(&g_highest_p_started), atomic_load(&g_highest_p_finished));
    for (int i = 0; i < count; i++) {
        fprintf(fp, "    {\"p\": %d, \"is_prime\": %s, \"iterations\": %lu, \"elapsed_ms\": %lu, \"status\": \"%s\"}%s\n",
                results[i]->p, results[i]->residue_is_zero ? "true" : "false",
                results[i]->iterations_done, results[i]->elapsed_ms,
                results[i]->status == STATUS_PRIME ? "PRIME" : "COMPOSITE",
                (i == count - 1) ? "" : ",");
    }
    fprintf(fp, "  ]\n}\n");
    fflush(fp); fsync(fileno(fp)); fclose(fp);
    rename("found_mersenne.json.tmp", "found_mersenne.json");
}

void* logger_loop(void* arg) {
    (void)arg;
    mersenne_task_t **results = NULL;
    int count = 0, capacity = 0;
    uint64_t last_save = ttak_get_tick_count();

    while (!atomic_load(&g_shutdown_requested) || count > 0) {
        mersenne_task_t *task = ttak_lf_queue_pop(&g_result_q);
        if (task) {
            if (task->status == STATUS_PRIME) {
                printf("\n[FOUND] M%d is prime!\n", task->p);
                fflush(stdout);
            }
            if (task->p > atomic_load(&g_highest_p_finished)) atomic_store(&g_highest_p_finished, task->p);
            
            if (count >= capacity) {
                capacity = capacity ? capacity * 2 : 100;
                results = realloc(results, sizeof(mersenne_task_t*) * capacity);
            }
            results[count++] = task;
        }

        uint64_t now = ttak_get_tick_count();
        if ((count > 0 && (count % 10 == 0 || now - last_save > 5000)) || (atomic_load(&g_shutdown_requested) && count > 0)) {
            save_state(results, count);
            last_save = now;
            if (atomic_load(&g_shutdown_requested) && ttak_lf_queue_pop(&g_result_q) == NULL) break;
        }
        if (!task) usleep(10000);
    }
    for (int i = 0; i < count; i++) free(results[i]);
    free(results);
    return NULL;
}

#ifdef TTAK_SELFTEST
void run_self_test() {
    int primes[] = {2, 3, 5, 7, 13, 17, 19, 31, 61, 89, 107, 127};
    int composites[] = {11, 23, 29};
    printf("[SELFTEST] Running Lucas-Lehmer verification...\n");
    for (size_t i = 0; i < sizeof(primes)/sizeof(int); i++) {
        mersenne_task_t t = {.p = primes[i]};
        lucas_lehmer_test(&t);
        printf(" M%d: %s\n", t.p, t.status == STATUS_PRIME ? "PASSED (PRIME)" : "FAILED");
    }
    for (size_t i = 0; i < sizeof(composites)/sizeof(int); i++) {
        mersenne_task_t t = {.p = composites[i]};
        lucas_lehmer_test(&t);
        printf(" M%d: %s\n", t.p, t.status == STATUS_COMPOSITE ? "PASSED (COMPOSITE)" : "FAILED");
    }
}
#endif

int main() {
#ifdef TTAK_SELFTEST
    run_self_test(); return 0;
#endif
    ttak_lf_queue_t task_q;
    ttak_lf_queue_init(&task_q);
    ttak_lf_queue_init(&g_result_q);

    struct sigaction sa = {.sa_handler = handle_sigint};
    sigaction(SIGINT, &sa, NULL);

    printf("TTAK Mersenne Explorer (Corrected FOUND Pipeline)\nPress Ctrl+C to stop.\n");
    
    ttak_thread_t workers[4], producer, logger;
    for (int i = 0; i < 4; i++) ttak_thread_create(&workers[i], worker_loop, &task_q);
    ttak_thread_create(&producer, producer_loop, &task_q);
    ttak_thread_create(&logger, logger_loop, NULL);

    while (!atomic_load(&g_shutdown_requested)) {
        usleep(1000000); 
        printf("\r[STATUS] Ops: %lu | Finished: M%d | Started: M%d   ", 
               atomic_load(&g_total_ops), atomic_load(&g_highest_p_finished), atomic_load(&g_highest_p_started));
        fflush(stdout);
    }

    printf("\nShutting down...\n");
    ttak_thread_join(producer, NULL);
    for (int i = 0; i < 4; i++) ttak_thread_join(workers[i], NULL);
    ttak_thread_join(logger, NULL);
    return 0;
}
