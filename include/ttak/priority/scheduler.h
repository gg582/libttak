#ifndef TTAK_PRIORITY_SCHEDULER_H
#define TTAK_PRIORITY_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>
#include <ttak/priority/nice.h>
#include <ttak/async/task.h>

typedef struct ttak_scheduler ttak_scheduler_t;

struct ttak_scheduler {
    int (*get_current_priority)(ttak_scheduler_t *sched);
    void (*set_priority_override)(ttak_scheduler_t *sched, ttak_task_t *task, int new_priority);
    
    /* Inspect series */
    size_t (*get_pending_count)(ttak_scheduler_t *sched);
    size_t (*get_running_count)(ttak_scheduler_t *sched);
    double (*get_load_average)(ttak_scheduler_t *sched);
};

ttak_scheduler_t *ttak_scheduler_get_instance(void);

#endif // TTAK_PRIORITY_SCHEDULER_H
