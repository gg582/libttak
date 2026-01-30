/**
 * @file task.c
 * @brief Implementation of task creation and execution.
 */

#include <ttak/async/task.h>
#include <ttak/mem/mem.h>
#include <stddef.h>

#include <ttak/async/promise.h>

/**
 * @brief Internal task structure.
 */
struct ttak_task {
    ttak_task_func_t func; /**< Function to execute. */
    void *arg;             /**< Argument for the function. */
    ttak_promise_t *promise; /**< Promise to fulfill. */
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
    }
    return task;
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
 * @brief Destroys the task and frees memory.
 * 
 * @param task Pointer to the task to destroy.
 */
void ttak_task_destroy(ttak_task_t *task, uint64_t now) {
    if (ttak_mem_access(task, now)) {
        ttak_mem_free(task);
    }
}
