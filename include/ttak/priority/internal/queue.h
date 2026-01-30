#ifndef __TTAK_INTERNAL_QUEUE_H__
#define __TTAK_INTERNAL_QUEUE_H__

#include <ttak/async/task.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

struct __internal_ttak_qnode_t {
    ttak_task_t *task;
    int priority;
    struct __internal_ttak_qnode_t *next;
};

struct __internal_ttak_proc_priority_queue_t {
    struct __internal_ttak_qnode_t *head;
    size_t size;
    size_t cap;

    void (*init)(struct __internal_ttak_proc_priority_queue_t *q);
    void (*push)(struct __internal_ttak_proc_priority_queue_t *q, ttak_task_t *task, int priority, uint64_t now);
    ttak_task_t *(*pop)(struct __internal_ttak_proc_priority_queue_t *q, uint64_t now);
    ttak_task_t *(*pop_blocking)(struct __internal_ttak_proc_priority_queue_t *q, pthread_mutex_t *mutex, pthread_cond_t *cond, uint64_t now);
    size_t (*get_size)(struct __internal_ttak_proc_priority_queue_t *q);
    size_t (*get_cap)(struct __internal_ttak_proc_priority_queue_t *q);
};

void ttak_priority_queue_init(struct __internal_ttak_proc_priority_queue_t *q);

#endif // __TTAK_INTERNAL_QUEUE_H__