#include <ttak/priority/scheduler.h>
#include <ttak/mem/mem.h>
#include <stddef.h>

static int sched_get_current_priority(ttak_scheduler_t *sched) {
    (void)sched;
    return 0; // Default priority
}

static void sched_set_priority_override(ttak_scheduler_t *sched, ttak_task_t *task, int new_priority) {
    (void)sched; (void)task; (void)new_priority;
}

static size_t sched_get_pending_count(ttak_scheduler_t *sched) {
    (void)sched;
    return 0;
}

static size_t sched_get_running_count(ttak_scheduler_t *sched) {
    (void)sched;
    return 0;
}

static double sched_get_load_average(ttak_scheduler_t *sched) {
    (void)sched;
    return 0.0;
}

static ttak_scheduler_t global_scheduler = {
    .get_current_priority = sched_get_current_priority,
    .set_priority_override = sched_set_priority_override,
    .get_pending_count = sched_get_pending_count,
    .get_running_count = sched_get_running_count,
    .get_load_average = sched_get_load_average
};

ttak_scheduler_t *ttak_scheduler_get_instance(void) {
    return &global_scheduler;
}
