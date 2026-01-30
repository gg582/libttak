#ifndef TTAK_THREAD_POOL_H
#define TTAK_THREAD_POOL_H

#include <stddef.h>
#include <stdint.h>
#include <ttak/async/task.h>
#include <ttak/thread/worker.h>
#include <ttak/priority/queue.h>

typedef struct ttak_thread_pool ttak_thread_pool_t;

struct ttak_thread_pool {
    size_t              num_threads;
    ttak_worker_t       **workers;
    __i_tt_proc_pq_t    task_queue;
    pthread_mutex_t     pool_lock;
    pthread_cond_t      task_cond;
    uint64_t            creation_ts;
    _Bool               is_shutdown;

    /**
     * @brief Kills all sub-threads.
     */
    void (*force_shutdown)(ttak_thread_pool_t *pool);
};

ttak_thread_pool_t *ttak_thread_pool_create(size_t num_threads, int default_nice, uint64_t now);
void ttak_thread_pool_destroy(ttak_thread_pool_t *pool);
ttak_future_t *ttak_thread_pool_submit_task(ttak_thread_pool_t *pool, void *(*func)(void *), void *arg, int priority, uint64_t now);

#endif // TTAK_THREAD_POOL_H