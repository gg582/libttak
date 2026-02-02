#include <ttak/mem_tree/mem_tree.h>
#include <ttak/mem/mem.h> // For ttak_mem_free
#include <ttak/timing/timing.h> // For ttak_get_tick_count
#include "../../internal/app_types.h" // For TT_SECOND, etc.
#include <stdlib.h>
#include <string.h>
#include <stdio.h> // For debugging

// Forward declaration for the cleanup thread function
static void *cleanup_thread_func(void *arg);

/**
 * @brief Initializes a new mem tree instance.
 *
 * This function sets up the initial state of the mem tree, including its mutex,
 * default cleanup interval, and manual cleanup flag. It also launches a background
 * thread responsible for automatic memory cleanup.
 *
 * @param tree Pointer to the ttak_mem_tree_t structure to initialize.
 */
void ttak_mem_tree_init(ttak_mem_tree_t *tree) {
    if (!tree) return;

    memset(tree, 0, sizeof(ttak_mem_tree_t));
    pthread_mutex_init(&tree->lock, NULL);
    atomic_store(&tree->cleanup_interval_ns, TT_MINUTE(30)); // Default to 30 minutes
    atomic_store(&tree->use_manual_cleanup, false);
    atomic_store(&tree->shutdown_requested, false);

    // Launch the background cleanup thread
    if (pthread_create(&tree->cleanup_thread, NULL, cleanup_thread_func, tree) != 0) {
        fprintf(stderr, "[TTAK_MEM_TREE] Failed to create cleanup thread.\n");
        // Handle error: perhaps set a flag to indicate auto-cleanup is disabled
    }
}

/**
 * @brief Destroys the mem tree, freeing all remaining allocated memory blocks and stopping the cleanup thread.
 *
 * This function signals the cleanup thread to terminate and waits for its completion.
 * It then iterates through any remaining mem nodes, frees their associated memory,
 * and destroys the mem tree's internal mutex.
 *
 * @param tree Pointer to the ttak_mem_tree_t structure to destroy.
 */
void ttak_mem_tree_destroy(ttak_mem_tree_t *tree) {
    if (!tree) return;

    // Signal cleanup thread to stop and join it
    atomic_store(&tree->shutdown_requested, true);
    if (tree->cleanup_thread) {
        pthread_join(tree->cleanup_thread, NULL);
    }

    pthread_mutex_lock(&tree->lock);
    ttak_mem_node_t *current = tree->head;
    while (current) {
        ttak_mem_node_t *next = current->next;
        // Free the actual memory block if it hasn't been freed already
        if (current->ptr) {
            ttak_mem_free(current->ptr);
        }
        pthread_mutex_destroy(&current->lock);
        free(current); // Free the mem node itself
        current = next;
    }
    tree->head = NULL;
    pthread_mutex_unlock(&tree->lock);
    pthread_mutex_destroy(&tree->lock);
}

/**
 * @brief Adds a new memory block to be tracked by the mem tree.
 *
 * A new mem node is created to encapsulate the provided memory block's metadata.
 * This node is then added to the mem tree's internal list. The initial reference
 * count is set to 1.
 *
 * @param tree Pointer to the mem tree.
 * @param ptr Pointer to the allocated memory block.
 * @param size Size of the allocated memory block.
 * @param expires_tick Monotonic tick when this memory block should expire.
 * @param is_root True if this is a root node (externally referenced).
 * @return A pointer to the newly created mem node, or NULL on failure.
 */
ttak_mem_node_t *ttak_mem_tree_add(ttak_mem_tree_t *tree, void *ptr, size_t size, uint64_t expires_tick, _Bool is_root) {
    if (!tree || !ptr) return NULL;

    ttak_mem_node_t *new_node = (ttak_mem_node_t *)malloc(sizeof(ttak_mem_node_t));
    if (!new_node) {
        fprintf(stderr, "[TTAK_MEM_TREE] Failed to allocate mem node.\n");
        return NULL;
    }

    new_node->ptr = ptr;
    new_node->size = size;
    new_node->expires_tick = expires_tick;
    atomic_init(&new_node->ref_count, 1); // Initial ref count is 1
    new_node->is_root = is_root;
    pthread_mutex_init(&new_node->lock, NULL);

    pthread_mutex_lock(&tree->lock);
    new_node->next = tree->head;
    tree->head = new_node;
    pthread_mutex_unlock(&tree->lock);

    return new_node;
}

/**
 * @brief Removes a memory block from the mem tree and frees it.
 *
 * This function is responsible for safely removing a mem node from the tree's
 * internal list and then freeing both the tracked memory block and the mem node
 * itself. It ensures thread-safe access to the tree structure.
 *
 * @param tree Pointer to the mem tree.
 * @param node Pointer to the mem node to remove and free.
 */
void ttak_mem_tree_remove(ttak_mem_tree_t *tree, ttak_mem_node_t *node) {
    if (!tree || !node) return;

    pthread_mutex_lock(&tree->lock);
    ttak_mem_node_t **indirect = &tree->head;
    while (*indirect != node) {
        indirect = &(*indirect)->next;
        if (!*indirect) { // Node not found
            pthread_mutex_unlock(&tree->lock);
            return;
        }
    }
    *indirect = node->next; // Remove from list

    pthread_mutex_unlock(&tree->lock);

    // Free the actual memory block
    if (node->ptr) {
        ttak_mem_free(node->ptr);
        node->ptr = NULL; // Prevent double free
    }
    pthread_mutex_destroy(&node->lock);
    free(node); // Free the mem node itself
}

/**
 * @brief Increments the reference count for a given mem node.
 *
 * This function atomically increments the reference count of a mem node,
 * indicating that another part of the system is now referencing this memory block.
 *
 * @param node Pointer to the mem node.
 */
void ttak_mem_node_acquire(ttak_mem_node_t *node) {
    if (!node) return;
    atomic_fetch_add(&node->ref_count, 1);
}

/**
 * @brief Decrements the reference count for a given mem node.
 *
 * This function atomically decrements the reference count. If the count drops
 * to zero, the node is considered unreferenced. If it's also expired, it becomes
 * a candidate for cleanup.
 *
 * @param node Pointer to the mem node.
 */
void ttak_mem_node_release(ttak_mem_node_t *node) {
    if (!node) return;
    atomic_fetch_sub(&node->ref_count, 1);
}

/**
 * @brief Sets the interval for automatic memory cleanup.
 *
 * This function updates the atomic cleanup interval for the mem tree.
 * The background cleanup thread will use this new interval for its sleep cycles.
 *
 * @param tree Pointer to the mem tree.
 * @param interval_ns The new cleanup interval in nanoseconds.
 */
void ttak_mem_tree_set_cleanup_interval(ttak_mem_tree_t *tree, uint64_t interval_ns) {
    if (!tree) return;
    atomic_store(&tree->cleanup_interval_ns, interval_ns);
}

/**
 * @brief Sets the manual cleanup flag.
 *
 * When set to true, automatic cleanup is disabled, and memory must be freed manually.
 * This function atomically updates the flag.
 *
 * @param tree Pointer to the mem tree.
 * @param manual_cleanup_enabled True to enable manual cleanup, false to enable automatic.
 */
void ttak_mem_tree_set_manual_cleanup(ttak_mem_tree_t *tree, _Bool manual_cleanup_enabled) {
    if (!tree) return;
    atomic_store(&tree->use_manual_cleanup, manual_cleanup_enabled);
}

/**
 * @brief Performs a manual cleanup pass, freeing expired and unreferenced memory blocks.
 *
 * This function iterates through all tracked mem nodes. If a node's reference count
 * is zero and its expiration time has passed (or it's marked for immediate cleanup),
 * its associated memory is freed, and the node is removed from the tree.
 *
 * @param tree Pointer to the mem tree.
 * @param now Current monotonic tick.
 */
void ttak_mem_tree_perform_cleanup(ttak_mem_tree_t *tree, uint64_t now) {
    if (!tree) return;

    pthread_mutex_lock(&tree->lock);
    ttak_mem_node_t **indirect = &tree->head;
    while (*indirect) {
        ttak_mem_node_t *node = *indirect;
        pthread_mutex_lock(&node->lock); // Lock node before checking its state

        _Bool should_free = false;
        if (atomic_load(&node->ref_count) == 0 && node->expires_tick != __TTAK_UNSAFE_MEM_FOREVER__ && now >= node->expires_tick) {
            should_free = true;
        }
        // Add condition for is_root and no external references if implemented later

        pthread_mutex_unlock(&node->lock); // Unlock node

        if (should_free) {
            *indirect = node->next; // Remove from list
            // Free the actual memory block
            if (node->ptr) {
                ttak_mem_free(node->ptr);
                node->ptr = NULL;
            }
            pthread_mutex_destroy(&node->lock);
            free(node); // Free the mem node itself
        } else {
            indirect = &(*indirect)->next;
        }
    }
    pthread_mutex_unlock(&tree->lock);
}

/**
 * @brief Background thread function for automatic memory cleanup.
 *
 * This thread periodically wakes up, checks if automatic cleanup is enabled,
 * and if so, performs a cleanup pass on the mem tree. It respects the configured
 * cleanup interval and terminates when a shutdown request is received.
 *
 * @param arg A pointer to the ttak_mem_tree_t instance.
 * @return NULL upon termination.
 */
static void *cleanup_thread_func(void *arg) {
    ttak_mem_tree_t *tree = (ttak_mem_tree_t *)arg;
    if (!tree) return NULL;

    while (!atomic_load(&tree->shutdown_requested)) {
        uint64_t interval_ns = atomic_load(&tree->cleanup_interval_ns);
        _Bool manual_cleanup_enabled = atomic_load(&tree->use_manual_cleanup);

        if (!manual_cleanup_enabled) {
            ttak_mem_tree_perform_cleanup(tree, ttak_get_tick_count());
        }

        // Sleep for the specified interval, but wake up if shutdown is requested
        uint64_t sleep_start = ttak_get_tick_count();
        while (ttak_get_tick_count() - sleep_start < interval_ns && !atomic_load(&tree->shutdown_requested)) {
            // Sleep in smaller chunks to be responsive to shutdown requests
            struct timespec ts = {.tv_sec = 0, .tv_nsec = TT_MILLI_SECOND(100)}; // Sleep for 100ms
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

/**
 * @brief Finds a mem node associated with a given memory pointer.
 *
 * This function traverses the mem tree's internal list to find the node
 * that tracks the specified memory pointer. It ensures thread-safe access
 * to the tree structure.
 *
 * @param tree Pointer to the mem tree.
 * @param ptr The memory pointer to search for.
 * @return A pointer to the found mem node, or NULL if not found.
 */
ttak_mem_node_t *ttak_mem_tree_find_node(ttak_mem_tree_t *tree, void *ptr) {
    if (!tree || !ptr) return NULL;

    pthread_mutex_lock(&tree->lock);
    ttak_mem_node_t *current = tree->head;
    while (current) {
        if (current->ptr == ptr) {
            pthread_mutex_unlock(&tree->lock);
            return current;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&tree->lock);
    return NULL;
}