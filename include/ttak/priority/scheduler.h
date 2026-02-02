#ifndef TTAK_PRIORITY_SCHEDULER_H
#define TTAK_PRIORITY_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>
#include <ttak/priority/nice.h>
#include <ttak/async/task.h>

typedef struct ttak_scheduler ttak_scheduler_t;

struct ttak_scheduler {
    int (*get_current_priority)(ttak_scheduler_t *sched);
    void (*set_priority_override)(ttak_scheduler_t *sched, ttak_task_t *task, int new_priority);
    
    /* Inspect series */
    size_t (*get_pending_count)(ttak_scheduler_t *sched);
    size_t (*get_running_count)(ttak_scheduler_t *sched);
    double (*get_load_average)(ttak_scheduler_t *sched);
};

ttak_scheduler_t *ttak_scheduler_get_instance(void);

/**
 * @brief Initialize the scheduler's internal history tracking.
 * Must be called once before using smart features.
 */
void ttak_scheduler_init(void);

/**
 * @brief Record a task's execution duration to update its history.
 * @param task The task that finished.
 * @param duration_ms Execution time in milliseconds.
 */
void ttak_scheduler_record_execution(ttak_task_t *task, uint64_t duration_ms);

/**
 * @brief Calculate an adjusted priority based on history (SJF) and user priority.
 * @param task The task to evaluate.
 * @param base_priority The requested priority (e.g. from thread pool submit).
 * @return Adjusted priority.
 */
int ttak_scheduler_get_adjusted_priority(ttak_task_t *task, int base_priority);

#endif // TTAK_PRIORITY_SCHEDULER_H
