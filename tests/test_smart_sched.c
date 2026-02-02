#include <ttak/priority/scheduler.h>
#include <ttak/async/task.h>
#include <ttak/timing/timing.h>
#include <ttak/mem/mem.h>
#include "test_macros.h"

void *dummy_short(void *arg) { return arg; }
void *dummy_long(void *arg) { return arg; }

void test_smart_scheduler_logic() {
    uint64_t now = ttak_get_tick_count();
    
    // 1. Init
    ttak_scheduler_init();

    // 2. Create tasks
    // Use different functions to ensure different hashes
    ttak_task_t *t_short = ttak_task_create(dummy_short, NULL, NULL, now);
    ttak_task_t *t_long = ttak_task_create(dummy_long, NULL, NULL, now);

    ASSERT(t_short != NULL);
    ASSERT(t_long != NULL);
    ASSERT(ttak_task_get_hash(t_short) != ttak_task_get_hash(t_long));

    // 3. Train "short" task (e.g. 5ms)
    // Run enough times to influence EMA
    for (int i = 0; i < 10; i++) {
        ttak_scheduler_record_execution(t_short, 5);
    }

    // 4. Train "long" task (e.g. 1000ms)
    for (int i = 0; i < 10; i++) {
        ttak_scheduler_record_execution(t_long, 1000);
    }

    // 5. Check adjustments
    int base = 0;
    int adj_short = ttak_scheduler_get_adjusted_priority(t_short, base);
    int adj_long = ttak_scheduler_get_adjusted_priority(t_long, base);

    // Short task should get a boost (e.g., +5 for <10ms)
    ASSERT(adj_short > base);
    
    // Long task should get a penalty (e.g., -2 for >500ms)
    ASSERT(adj_long < base);

    // Cleanup
    ttak_task_destroy(t_short, now);
    ttak_task_destroy(t_long, now);
}

int main() {
    RUN_TEST(test_smart_scheduler_logic);
    return 0;
}
