#include <ttak/thread/pool.h>
#include <ttak/mem/mem.h>
#include <ttak/sync/sync.h>
#include <ttak/async/promise.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include <ttak/priority/scheduler.h>

/**
 * @brief Stop all workers and signal shutdown.
 *
 * @param pool Pool to shut down.
 */
static void pool_force_shutdown(ttak_thread_pool_t *pool) {
    if (!pool) return;
    pthread_mutex_lock(&pool->pool_lock);
    pool->is_shutdown = true;
    for (size_t i = 0; i < pool->num_threads; i++) {
        if (pool->workers[i]) {
            pool->workers[i]->should_stop = true;
        }
    }
    pthread_cond_broadcast(&pool->task_cond);
    pthread_mutex_unlock(&pool->pool_lock);
}

/**
 * @brief Create a thread pool with the given worker count.
 *
 * @param num_threads Number of worker threads.
 * @param default_nice Initial nice value for workers.
 * @param now         Timestamp for memory tracking.
 * @return Pointer to the created pool or NULL on failure.
 */
ttak_thread_pool_t *ttak_thread_pool_create(size_t num_threads, int default_nice, uint64_t now) {
    // Ensure smart scheduler is ready
    ttak_scheduler_init();

    ttak_thread_pool_t *pool = (ttak_thread_pool_t *)ttak_mem_alloc(sizeof(ttak_thread_pool_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!pool) return NULL;

    pool->num_threads = num_threads;
    pool->creation_ts = now;
    pool->is_shutdown = false;
    pool->force_shutdown = pool_force_shutdown;

    pthread_mutex_init(&pool->pool_lock, NULL);
    pthread_cond_init(&pool->task_cond, NULL);
    ttak_priority_queue_init(&pool->task_queue);

    pool->workers = (ttak_worker_t **)ttak_mem_alloc(sizeof(ttak_worker_t *) * num_threads, __TTAK_UNSAFE_MEM_FOREVER__, now);

    for (size_t i = 0; i < num_threads; i++) {
        pool->workers[i] = (ttak_worker_t *)ttak_mem_alloc(sizeof(ttak_worker_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
        pool->workers[i]->pool = pool;
        pool->workers[i]->should_stop = false;
        pool->workers[i]->exit_code = 0;
        
        pool->workers[i]->wrapper = (ttak_worker_wrapper_t *)ttak_mem_alloc(sizeof(ttak_worker_wrapper_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
        pool->workers[i]->wrapper->nice_val = default_nice;
        pool->workers[i]->wrapper->ts = now;

        pthread_create(&pool->workers[i]->thread, NULL, ttak_worker_routine, pool->workers[i]);
    }

    return pool;
}

/**
 * @brief Submit a function to be executed asynchronously.
 *
 * @param pool     Pool receiving the work.
 * @param func     Function pointer to execute.
 * @param arg      Argument passed to the function.
 * @param priority Scheduling priority hint.
 * @param now      Timestamp for memory bookkeeping.
 * @return Future representing the eventual result, or NULL on failure.
 */
ttak_future_t *ttak_thread_pool_submit_task(ttak_thread_pool_t *pool, void *(*func)(void *), void *arg, int priority, uint64_t now) {
    if (!pool) return NULL;

    ttak_promise_t *promise = ttak_promise_create(now);
    if (!promise) return NULL;

    ttak_task_t *task = ttak_task_create((ttak_task_func_t)func, arg, promise, now);
    if (!task) {
        ttak_mem_free(promise->future);
        ttak_mem_free(promise); // destroys promise here.
        return NULL; 
    }

    // Apply smart scheduling adjustment
    int adjusted_priority = ttak_scheduler_get_adjusted_priority(task, priority);

    if (!ttak_thread_pool_schedule_task(pool, task, adjusted_priority, now)) {
        ttak_task_destroy(task, now);
        ttak_mem_free(promise->future);
        ttak_mem_free(promise);
        return NULL;
    }

    return ttak_promise_get_future(promise);
}

/**
 * @brief Queue a prepared task for execution.
 *
 * @param pool     Pool to enqueue into.
 * @param task     Task instance created earlier.
 * @param priority Priority hint.
 * @param now      Timestamp for queue bookkeeping.
 * @return true if scheduled, false if the pool is shutting down.
 */
_Bool ttak_thread_pool_schedule_task(ttak_thread_pool_t *pool, ttak_task_t *task, int priority, uint64_t now) {
    if (!pool || !task) return 0;

    pthread_mutex_lock(&pool->pool_lock);
    if (pool->is_shutdown) {
        pthread_mutex_unlock(&pool->pool_lock);
        return 0;
    }

    pool->task_queue.push(&pool->task_queue, task, priority, now);
    pthread_cond_signal(&pool->task_cond);
    pthread_mutex_unlock(&pool->pool_lock);

    return 1;
}

/**
 * @brief Destroy the pool, wait for workers, and free pending tasks.
 *
 * @param pool Pool to destroy.
 */
void ttak_thread_pool_destroy(ttak_thread_pool_t *pool) {
    if (!pool) return;

    pool_force_shutdown(pool);

    for (size_t i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->workers[i]->thread, NULL);
        ttak_mem_free(pool->workers[i]->wrapper);
        ttak_mem_free(pool->workers[i]);
    }

    ttak_mem_free(pool->workers);
    pthread_mutex_destroy(&pool->pool_lock);
    pthread_cond_destroy(&pool->task_cond);
    
    ttak_task_t *t;
    while ((t = pool->task_queue.pop(&pool->task_queue, pool->creation_ts)) != NULL) {
        ttak_task_destroy(t, pool->creation_ts);
    }

    ttak_mem_free(pool);
}
