#include <ttak/thread/worker.h>
#include <ttak/thread/pool.h>
#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>
#include <ttak/priority/scheduler.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stdlib.h>

/**
 * @brief Threaded function wrapper that validates memory every tick.
 */
static void threaded_function_wrapper(ttak_worker_t *worker, ttak_task_t *task) {
    (void)worker;
    uint64_t now = ttak_get_tick_count();
    
    // Validate all tracked pointers for this task (demonstration)
    size_t count = 0;
    void **dirty = tt_inspect_dirty_pointers(now, &count);
    if (dirty) {
        // If critical memory for task is dirty, we might need to longjmp
        // For now, autoclean
        tt_autoclean_dirty_pointers(now);
        free(dirty);
    }

    if (task) {
        uint64_t start_time = ttak_get_tick_count();
        ttak_task_set_start_ts(task, start_time);
        
        ttak_task_execute(task, now);
        
        uint64_t end_time = ttak_get_tick_count();
        uint64_t duration = (end_time >= start_time) ? (end_time - start_time) : 0;
        
        ttak_scheduler_record_execution(task, duration);
    }
}

/**
 * @brief Worker thread entry point that drains the pool queue.
 *
 * @param arg Pointer to the worker structure.
 * @return Exit code cast to void*.
 */
void *ttak_worker_routine(void *arg) {
    ttak_worker_t *self = (ttak_worker_t *)arg;
    ttak_thread_pool_t *pool = self->pool;

    if (self->wrapper) {
        setpriority(PRIO_PROCESS, 0, self->wrapper->nice_val);
    }

    while (!self->should_stop) {
        pthread_mutex_lock(&pool->pool_lock);
        while (pool->task_queue.head == NULL && !self->should_stop && !pool->is_shutdown) {
            pthread_cond_wait(&pool->task_cond, &pool->pool_lock);
        }

        if (self->should_stop || pool->is_shutdown) {
            pthread_mutex_unlock(&pool->pool_lock);
            break;
        }

        uint64_t now = ttak_get_tick_count();
        ttak_task_t *task = pool->task_queue.pop(&pool->task_queue, now);
        pthread_mutex_unlock(&pool->pool_lock);

        if (task) {
            if (setjmp(self->wrapper->env) == 0) {
                threaded_function_wrapper(self, task);
            } else {
                // Recovered from longjmp
                self->exit_code = TTAK_ERR_FATAL_EXIT;
            }
            ttak_task_destroy(task, now);
        }
    }

    return (void *)(uintptr_t)self->exit_code;
}
