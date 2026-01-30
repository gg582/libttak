#include <ttak/priority/internal/queue.h>
#include <ttak/mem/mem.h>
#include <stddef.h>

static void q_push(struct __internal_ttak_proc_priority_queue_t *q, ttak_task_t *task, int priority, uint64_t now) {
    if (!q) return;
    struct __internal_ttak_qnode_t *node = (struct __internal_ttak_qnode_t *)ttak_mem_alloc(sizeof(struct __internal_ttak_qnode_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) return;
    node->task = task;
    node->priority = priority;
    node->next = NULL;
    if (!q->head || q->head->priority < priority) {
        node->next = q->head;
        q->head = node;
    } else {
        struct __internal_ttak_qnode_t *current = q->head;
        while (current->next && current->next->priority >= priority) {
            current = current->next;
        }
        node->next = current->next;
        current->next = node;
    }
    q->size++;
}

static ttak_task_t *q_pop(struct __internal_ttak_proc_priority_queue_t *q, uint64_t now) {
    if (!q || !q->head) return NULL;
    struct __internal_ttak_qnode_t *node = q->head;
    if (!ttak_mem_access(node, now)) return NULL;
    
    ttak_task_t *task = node->task;
    q->head = node->next;
    q->size--;
    ttak_mem_free(node);
    return task;
}

static ttak_task_t *q_pop_blocking(struct __internal_ttak_proc_priority_queue_t *q, pthread_mutex_t *mutex, pthread_cond_t *cond, uint64_t now) {
    if (!q) return NULL;
    while (q->head == NULL) {
        pthread_cond_wait(cond, mutex);
    }
    return q_pop(q, now);
}

static size_t q_get_size(struct __internal_ttak_proc_priority_queue_t *q) {
    return q ? q->size : 0;
}

static size_t q_get_cap(struct __internal_ttak_proc_priority_queue_t *q) {
    return q ? q->cap : 0;
}

void ttak_priority_queue_init(struct __internal_ttak_proc_priority_queue_t *q) {
    if (!q) return;
    q->head = NULL;
    q->size = 0;
    q->cap = 0;
    q->push = q_push;
    q->pop = q_pop;
    q->pop_blocking = q_pop_blocking;
    q->get_size = q_get_size;
    q->get_cap = q_get_cap;
}
