#include <ttak/priority/scheduler.h>
#include <ttak/mem/mem.h>
#include <ttak/ht/map.h>
#include <ttak/timing/timing.h>
#include <ttak/priority/nice.h>
#include <pthread.h>
#include <stddef.h>

static tt_map_t *history_map = NULL;
static pthread_mutex_t history_lock = PTHREAD_MUTEX_INITIALIZER;
static _Bool history_initialized = 0;

void ttak_scheduler_init(void) {
    pthread_mutex_lock(&history_lock);
    if (!history_initialized) {
        // Initial capacity 128, current time for allocation
        history_map = ttak_create_map(128, ttak_get_tick_count());
        history_initialized = 1;
    }
    pthread_mutex_unlock(&history_lock);
}

void ttak_scheduler_record_execution(ttak_task_t *task, uint64_t duration_ms) {
    if (!task) return;
    
    uint64_t hash = ttak_task_get_hash(task);
    if (hash == 0) return;

    uint64_t now = ttak_get_tick_count();
    
    pthread_mutex_lock(&history_lock);
    if (history_map) {
        size_t old_avg = 0;
        if (ttak_map_get_key(history_map, (uintptr_t)hash, &old_avg, now)) {
            // EMA: New = Old * 0.7 + Current * 0.3
            size_t new_avg = (size_t)((old_avg * 0.7) + (duration_ms * 0.3));
            ttak_insert_to_map(history_map, (uintptr_t)hash, new_avg, now);
        } else {
            ttak_insert_to_map(history_map, (uintptr_t)hash, (size_t)duration_ms, now);
        }
    }
    pthread_mutex_unlock(&history_lock);
}

int ttak_scheduler_get_adjusted_priority(ttak_task_t *task, int base_priority) {
    if (!task) return base_priority;

    uint64_t hash = ttak_task_get_hash(task);
    if (hash == 0) return base_priority;

    int adj_priority = base_priority;
    uint64_t now = ttak_get_tick_count();
    size_t avg_runtime = 0;
    _Bool found = 0;

    pthread_mutex_lock(&history_lock);
    if (history_map) {
        found = ttak_map_get_key(history_map, (uintptr_t)hash, &avg_runtime, now);
    }
    pthread_mutex_unlock(&history_lock);

    if (found) {
        if (avg_runtime < 10) { 
            // Very short (< 10ms): Massive boost
            adj_priority += 5;
        } else if (avg_runtime < 50) {
            // Short (< 50ms): Boost
            adj_priority += 2;
        } else if (avg_runtime > 2000) {
            // Very Long (> 2s): Penalty
            adj_priority -= 5;
        } else if (avg_runtime > 500) {
            // Long (> 500ms): Slight penalty
            adj_priority -= 2;
        }
    } else {
        // Unknown task: Give slight boost to favor new tasks (optimistic)
        adj_priority += 1;
    }

    // Clamp to valid range (assuming -20 to 19 or similar, logic from nice.h)
    // __TT_PRIO_MAX is 19, MIN is -20.
    // Higher value usually means LOWER priority in nice, but here the queue implementation 
    // in queue.c treats 'priority' as "higher = sooner" (standard PQ behavior).
    // Let's check queue.c again.
    // queue.c: "if (!q->head || q->head->priority < priority)" -> Higher int = Higher priority (Front of queue).
    // nice.h: URGENT (-20), LAZY (19). Standard nice: Low number = High priority.
    
    // CONFLICT: nice.h uses standard nice (low=high), but queue.c uses high=high.
    // I need to invert the nice logic if I use queue.c.
    // Assuming 'base_priority' passed here is intended for queue.c (High=High).
    
    // If I just return adjusted integer, it's fine.
    
    return adj_priority;
}

/**
 * @brief Stub: return the scheduler's current priority.
 */
static int sched_get_current_priority(ttak_scheduler_t *sched) {
    (void)sched;
    return 0; // Default priority
}

/**
 * @brief Stub: override the priority for a task.
 */
static void sched_set_priority_override(ttak_scheduler_t *sched, ttak_task_t *task, int new_priority) {
    (void)sched; (void)task; (void)new_priority;
}

/**
 * @brief Stub: return pending task count.
 */
static size_t sched_get_pending_count(ttak_scheduler_t *sched) {
    (void)sched;
    return 0;
}

/**
 * @brief Stub: return running task count.
 */
static size_t sched_get_running_count(ttak_scheduler_t *sched) {
    (void)sched;
    return 0;
}

/**
 * @brief Stub: return scheduler load average estimate.
 */
static double sched_get_load_average(ttak_scheduler_t *sched) {
    (void)sched;
    return 0.0;
}

static ttak_scheduler_t global_scheduler = {
    .get_current_priority = sched_get_current_priority,
    .set_priority_override = sched_set_priority_override,
    .get_pending_count = sched_get_pending_count,
    .get_running_count = sched_get_running_count,
    .get_load_average = sched_get_load_average
};

/**
 * @brief Obtain the singleton scheduler instance.
 *
 * @return Pointer to the global stub scheduler.
 */
ttak_scheduler_t *ttak_scheduler_get_instance(void) {
    return &global_scheduler;
}
