#include <ttak/async/sched.h>
#include <ttak/thread/pool.h>
#include <stddef.h>

void ttak_async_schedule(ttak_task_t *task, uint64_t now) {
    if (!task) return;
    // Basic implementation: execute immediately
    ttak_task_execute(task, now);
}

void ttak_async_yield(void) {
    // Simple yield
}
