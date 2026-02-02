/**
 * @file task.c
 * @brief Implementation of task creation and execution.
 */

#include <ttak/async/task.h>
#include <ttak/mem/mem.h>
#include <ttak/ht/hash.h>
#include <stddef.h>

#include <ttak/async/promise.h>

/**
 * @brief Internal task structure.
 */
struct ttak_task {
    ttak_task_func_t func; /**< Function to execute. */
    void *arg;             /**< Argument for the function. */
    ttak_promise_t *promise; /**< Promise to fulfill. */
    uint64_t task_hash;      /**< Hash to identify task type. */
    uint64_t start_ts;       /**< Execution start timestamp. */
    int base_priority;       /**< Original user priority. */
};

/**
 * @brief Creates a new task.
 * 
 * Allocates memory for the task and initializes it.
 * 
 * @param func Function to execute.
 * @param arg Argument for the function.
 * @param promise Promise to fulfill when the task is complete.
 * @return Pointer to the created task, or NULL on failure.
 */
ttak_task_t *ttak_task_create(ttak_task_func_t func, void *arg, ttak_promise_t *promise, uint64_t now) {
    ttak_task_t *task = (ttak_task_t *)ttak_mem_alloc(sizeof(ttak_task_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (task) {
        task->func = func;
        task->arg = arg;
        task->promise = promise;
        
        // Use SipHash-2-4 with arbitrary keys for task fingerprinting
        uint64_t k0 = 0x0706050403020100ULL;
        uint64_t k1 = 0x0F0E0D0C0B0A0908ULL;
        // Combine func and arg into a pseudo-key or hash them separately and combine?
        // Since the hash function takes a uintptr_t key, let's mix them first or just hash one heavily.
        // Better: Hash the pointer to the function, and XOR with hash of arg?
        // Or just XOR the pointers and hash the result?
        // Let's do: Hash(func ^ arg). Simple and consistent.
        uintptr_t combined = (uintptr_t)func ^ (uintptr_t)arg;
        task->task_hash = gen_hash_sip24(combined, k0, k1);
        
        task->start_ts = 0;
        task->base_priority = 0;
    }
    return task;
}

void ttak_task_set_hash(ttak_task_t *task, uint64_t hash) {
    if (task) task->task_hash = hash;
}

uint64_t ttak_task_get_hash(const ttak_task_t *task) {
    return task ? task->task_hash : 0;
}

void ttak_task_set_start_ts(ttak_task_t *task, uint64_t ts) {
    if (task) task->start_ts = ts;
}

uint64_t ttak_task_get_start_ts(const ttak_task_t *task) {
    return task ? task->start_ts : 0;
}

/**
 * @brief Executes the task.
 * 
 * @param task Pointer to the task.
 */
void ttak_task_execute(ttak_task_t *task, uint64_t now) {
    if (ttak_mem_access(task, now) && task->func) {
        void *res = task->func(task->arg);
        if (task->promise) {
            ttak_promise_set_value(task->promise, res, now);
        }
    }
}

/**
 * @brief Creates a duplicate of the provided task.
 *
 * @param task Task to duplicate.
 * @param now Current timestamp for memory tracking.
 * @return Pointer to the cloned task or NULL on failure.
 */
ttak_task_t *ttak_task_clone(const ttak_task_t *task, uint64_t now) {
    if (!ttak_mem_access((void *)task, now)) return NULL;
    return ttak_task_create(task->func, task->arg, task->promise, now);
}

/**
 * @brief Destroys the task and frees memory.
 * 
 * @param task Pointer to the task to destroy.
 */
void ttak_task_destroy(ttak_task_t *task, uint64_t now) {
    if (ttak_mem_access(task, now)) {
        ttak_mem_free(task);
    }
}
