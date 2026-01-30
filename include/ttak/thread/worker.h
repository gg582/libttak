#ifndef TTAK_THREAD_WORKER_H
#define TTAK_THREAD_WORKER_H

#include <pthread.h>
#include <setjmp.h>
#include <stdint.h>
#include <ttak/async/promise.h>

#define TTAK_ERR_JOIN_FAILED     -101
#define TTAK_ERR_SHUTDOWN_RETRY  -102
#define TTAK_ERR_FATAL_EXIT      -103

typedef struct ttak_worker_wrapper {
    void            *(*func)(void *);
    void            *arg;
    ttak_promise_t  *promise;
    jmp_buf         env;
    uint64_t        ts;
    int             nice_val;
} ttak_worker_wrapper_t;

typedef struct ttak_worker {
    pthread_t               thread;
    struct ttak_thread_pool *pool;
    ttak_worker_wrapper_t   *wrapper;
    _Bool                   should_stop;
    int                     exit_code;
} ttak_worker_t;

void *ttak_worker_routine(void *arg);

#endif // TTAK_THREAD_WORKER_H
