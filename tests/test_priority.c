#include <ttak/priority/queue.h>
#include <ttak/async/task.h>
#include "test_macros.h"

void *dummy_func(void *arg) { (void)arg; return NULL; }

void test_priority_queue_basic() {
    struct __internal_ttak_proc_priority_queue_t q;
    ttak_priority_queue_init(&q);
    
    uint64_t now = 100;
    ttak_task_t *t1 = ttak_task_create(dummy_func, NULL, NULL, now);
    ttak_task_t *t2 = ttak_task_create(dummy_func, NULL, NULL, now);
    
    q.push(&q, t1, 10, now);
    q.push(&q, t2, 20, now); // Higher priority
    
    ASSERT(q.get_size(&q) == 2);
    
    ttak_task_t *p1 = q.pop(&q, now);
    ASSERT(p1 == t2); // t2 has higher priority (20 > 10)
    
    ttak_task_t *p2 = q.pop(&q, now);
    ASSERT(p2 == t1);
    
    ASSERT(q.get_size(&q) == 0);
    
    ttak_task_destroy(t1, now);
    ttak_task_destroy(t2, now);
}

int main() {
    RUN_TEST(test_priority_queue_basic);
    return 0;
}
