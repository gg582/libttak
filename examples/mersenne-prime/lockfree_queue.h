#ifndef TTAK_LOCKFREE_QUEUE_H
#define TTAK_LOCKFREE_QUEUE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Simple Single-Producer Multi-Consumer (SPMC) Lock-Free Queue.
 * For Multi-Producer use, wrap ttak_lf_queue_push in a mutex.
 */
#define TTAK_LF_QUEUE_SIZE 1024

typedef struct {
    atomic_size_t head;
    atomic_size_t tail;
    void* buffer[TTAK_LF_QUEUE_SIZE];
} ttak_lf_queue_t;

static inline void ttak_lf_queue_init(ttak_lf_queue_t *q) {
    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);
}

static inline bool ttak_lf_queue_push(ttak_lf_queue_t *q, void *data) {
    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    size_t next_tail = (tail + 1) % TTAK_LF_QUEUE_SIZE;
    if (next_tail == atomic_load_explicit(&q->head, memory_order_acquire)) {
        return false; // Full
    }
    q->buffer[tail] = data;
    // Release fence ensures the consumer sees the data in the buffer before the new tail.
    atomic_store_explicit(&q->tail, next_tail, memory_order_release);
    return true;
}

static inline void* ttak_lf_queue_pop(ttak_lf_queue_t *q) {
    size_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    while (true) {
        // Acquire fence ensures we see the data written by the producer.
        size_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
        if (head == tail) {
            return NULL; // Empty
        }
        void *data = q->buffer[head];
        if (atomic_compare_exchange_weak_explicit(&q->head, &head, (head + 1) % TTAK_LF_QUEUE_SIZE, memory_order_release, memory_order_relaxed)) {
            return data;
        }
        // head is updated by CAS on failure, retry.
    }
}

#endif // TTAK_LOCKFREE_QUEUE_H