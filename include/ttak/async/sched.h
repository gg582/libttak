#ifndef TTAK_ASYNC_SCHED_H
#define TTAK_ASYNC_SCHED_H

#include <ttak/async/task.h>
#include <stdint.h>

void ttak_async_schedule(ttak_task_t *task, uint64_t now);
void ttak_async_yield(void);

#endif // TTAK_ASYNC_SCHED_H
