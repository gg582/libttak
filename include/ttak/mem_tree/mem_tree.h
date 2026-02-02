#ifndef TTAK_MEM_TREE_H
#define TTAK_MEM_TREE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>

/**
 * @brief Represents a node in the generic heap tree, tracking a dynamically allocated memory block.
 *
 * Each node stores metadata about a memory allocation, including its pointer, size,
 * expiration time, and reference count. It also includes flags for managing its
 * lifecycle within the heap tree and a mutex for thread-safe access.
 */
typedef struct ttak_mem_node {
    void *ptr;                      /**< Pointer to the actual memory block. */
    size_t size;                    /**< Size of the allocated memory block. */
    uint64_t expires_tick;          /**< Monotonic tick when this memory block should expire. */
    _Atomic uint32_t ref_count;     /**< Atomic count of references to this node. */
    _Bool is_root;                  /**< True if this node is referenced externally (not by another heap node). */
    pthread_mutex_t lock;           /**< Mutex for thread-safe access to this node's metadata. */
    struct ttak_mem_node *next;    /**< Pointer to the next node in the mem tree's internal list. */
} ttak_mem_node_t;

/**
 * @brief Manages the collection of dynamically allocated memory blocks as a mem tree.
 *
 * This structure provides a centralized mechanism for tracking memory allocations,
 * their interdependencies (via reference counts), and their lifetimes. It supports
 * automatic cleanup of expired or unreferenced blocks and allows for manual control
 * over the cleanup process.
 */
typedef struct ttak_mem_tree {
    ttak_mem_node_t *head;             /**< Head of the linked list of all tracked mem nodes. */
    pthread_mutex_t lock;               /**< Mutex for thread-safe access to the mem tree structure. */
    _Atomic uint64_t cleanup_interval_ns; /**< Interval in nanoseconds for automatic cleanup. */
    _Atomic _Bool use_manual_cleanup;   /**< Flag to disable automatic cleanup (1 for manual, 0 for auto). */
    pthread_t cleanup_thread;           /**< Thread ID for the background automatic cleanup process. */
    _Atomic _Bool shutdown_requested;   /**< Flag to signal the cleanup thread to terminate. */
} ttak_mem_tree_t;

/**
 * @brief Initializes a new mem tree instance.
 *
 * @param tree Pointer to the ttak_mem_tree_t structure to initialize.
 */
void ttak_mem_tree_init(ttak_mem_tree_t *tree);

/**
 * @brief Destroys the mem tree, freeing all remaining allocated memory blocks and stopping the cleanup thread.
 *
 * @param tree Pointer to the ttak_mem_tree_t structure to destroy.
 */
void ttak_mem_tree_destroy(ttak_mem_tree_t *tree);

/**
 * @brief Adds a new memory block to be tracked by the mem tree.
 *
 * @param tree Pointer to the mem tree.
 * @param ptr Pointer to the allocated memory block.
 * @param size Size of the allocated memory block.
 * @param expires_tick Monotonic tick when this memory block should expire.
 * @param is_root True if this is a root node (externally referenced).
 * @return A pointer to the newly created mem node, or NULL on failure.
 */
ttak_mem_node_t *ttak_mem_tree_add(ttak_mem_tree_t *tree, void *ptr, size_t size, uint64_t expires_tick, _Bool is_root);

/**
 * @brief Removes a memory block from the mem tree and frees it.
 *
 * This function should only be called when the memory block is no longer referenced
 * and its lifetime has expired, or when explicitly forced.
 *
 * @param tree Pointer to the mem tree.
 * @param node Pointer to the mem node to remove and free.
 */
void ttak_mem_tree_remove(ttak_mem_tree_t *tree, ttak_mem_node_t *node);

/**
 * @brief Increments the reference count for a given mem node.
 *
 * @param node Pointer to the mem node.
 */
void ttak_mem_node_acquire(ttak_mem_node_t *node);

/**
 * @brief Decrements the reference count for a given mem node.
 *
 * If the reference count drops to zero and the node has expired, it may be
 * marked for cleanup or immediately freed depending on the mem tree's policy.
 *
 * @param node Pointer to the mem node.
 */
void ttak_mem_node_release(ttak_mem_node_t *node);

/**
 * @brief Sets the interval for automatic memory cleanup.
 *
 * @param tree Pointer to the mem tree.
 * @param interval_ns The new cleanup interval in nanoseconds.
 */
void ttak_mem_tree_set_cleanup_interval(ttak_mem_tree_t *tree, uint64_t interval_ns);

/**
 * @brief Sets the manual cleanup flag.
 *
 * When set to true, automatic cleanup is disabled, and memory must be freed manually.
 *
 * @param tree Pointer to the mem tree.
 * @param manual_cleanup_enabled True to enable manual cleanup, false to enable automatic.
 */
void ttak_mem_tree_set_manual_cleanup(ttak_mem_tree_t *tree, _Bool manual_cleanup_enabled);

/**
 * @brief Performs a manual cleanup pass, freeing expired and unreferenced memory blocks.
 *
 * This function is typically called when automatic cleanup is disabled or when
 * an immediate cleanup is desired.
 *
 * @param tree Pointer to the mem tree.
 * @param now Current monotonic tick.
 */
void ttak_mem_tree_perform_cleanup(ttak_mem_tree_t *tree, uint64_t now);

/**
 * @brief Finds a mem node associated with a given memory pointer.
 *
 * @param tree Pointer to the mem tree.
 * @param ptr The memory pointer to search for.
 * @return A pointer to the found mem node, or NULL if not found.
 */
ttak_mem_node_t *ttak_mem_tree_find_node(ttak_mem_tree_t *tree, void *ptr);

#endif // TTAK_HEAP_TREE_H
