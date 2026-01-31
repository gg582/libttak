#ifndef TTAK_PRIORITY_SIMPLE_H
#define TTAK_PRIORITY_SIMPLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Generic singly linked list node used by simple stacks and queues.
 */
typedef struct ttak_simple_node {
    void *data;
    struct ttak_simple_node *next;
} ttak_simple_node_t;

/**
 * @brief Generic FIFO queue implemented with linked nodes.
 */
typedef struct {
    ttak_simple_node_t *head;
    ttak_simple_node_t *tail;
    size_t size;
} ttak_simple_queue_t;

/**
 * @brief Generic LIFO stack implemented with linked nodes.
 */
typedef struct {
    ttak_simple_node_t *top;
    size_t size;
} ttak_simple_stack_t;

/**
 * @brief Initialize a queue before first use.
 *
 * @param q Queue instance to initialize.
 */
void ttak_simple_queue_init(ttak_simple_queue_t *q);
/**
 * @brief Enqueue an element at the tail.
 *
 * @param q Target queue.
 * @param data Pointer to store.
 * @param now Timestamp/epoch forwarded to profiling hooks.
 */
void ttak_simple_queue_push(ttak_simple_queue_t *q, void *data, uint64_t now);
/**
 * @brief Remove and return the head element.
 *
 * @param q Target queue.
 * @param now Timestamp/epoch forwarded to profiling hooks.
 * @return Stored pointer or NULL when empty.
 */
void *ttak_simple_queue_pop(ttak_simple_queue_t *q, uint64_t now);
/**
 * @brief Check whether the queue has no elements.
 *
 * @param q Queue to inspect.
 * @return true when the queue is empty, false otherwise.
 */
bool ttak_simple_queue_is_empty(const ttak_simple_queue_t *q);
/**
 * @brief Return the number of enqueued elements.
 *
 * @param q Queue to inspect.
 * @return Element count.
 */
size_t ttak_simple_queue_size(const ttak_simple_queue_t *q);
/**
 * @brief Release any resources associated with the queue.
 *
 * @param q Queue to destroy.
 * @param now Timestamp/epoch forwarded to profiling hooks.
 */
void ttak_simple_queue_destroy(ttak_simple_queue_t *q, uint64_t now);

/**
 * @brief Initialize a stack before first use.
 *
 * @param s Stack instance to initialize.
 */
void ttak_simple_stack_init(ttak_simple_stack_t *s);
/**
 * @brief Push an element onto the stack.
 *
 * @param s Target stack.
 * @param data Pointer to store.
 * @param now Timestamp/epoch forwarded to profiling hooks.
 */
void ttak_simple_stack_push(ttak_simple_stack_t *s, void *data, uint64_t now);
/**
 * @brief Pop the top element from the stack.
 *
 * @param s Target stack.
 * @param now Timestamp/epoch forwarded to profiling hooks.
 * @return Stored pointer or NULL when empty.
 */
void *ttak_simple_stack_pop(ttak_simple_stack_t *s, uint64_t now);
/**
 * @brief Check whether the stack is empty.
 *
 * @param s Stack to inspect.
 * @return true when the stack is empty, false otherwise.
 */
bool ttak_simple_stack_is_empty(const ttak_simple_stack_t *s);
/**
 * @brief Return the number of elements stored in the stack.
 *
 * @param s Stack to inspect.
 * @return Element count.
 */
size_t ttak_simple_stack_size(const ttak_simple_stack_t *s);
/**
 * @brief Release any resources associated with the stack.
 *
 * @param s Stack to destroy.
 * @param now Timestamp/epoch forwarded to profiling hooks.
 */
void ttak_simple_stack_destroy(ttak_simple_stack_t *s, uint64_t now);

#endif // TTAK_PRIORITY_SIMPLE_H
