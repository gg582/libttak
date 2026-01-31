#include "test_macros.h"
#include <ttak/priority/simple.h>
#include <ttak/priority/nice.h>
#include <stdint.h>

void test_simple_queue() {
    ttak_simple_queue_t q;
    ttak_simple_queue_init(&q);
    uint64_t now = 1000;

    int a = 1, b = 2, c = 3;

    ttak_simple_queue_push(&q, &a, now);
    ttak_simple_queue_push(&q, &b, now);
    ttak_simple_queue_push(&q, &c, now);

    ASSERT(ttak_simple_queue_size(&q) == 3);

    void *p1 = ttak_simple_queue_pop(&q, now);
    ASSERT(p1 == &a);

    void *p2 = ttak_simple_queue_pop(&q, now);
    ASSERT(p2 == &b);

    void *p3 = ttak_simple_queue_pop(&q, now);
    ASSERT(p3 == &c);

    ASSERT(ttak_simple_queue_is_empty(&q));
    ttak_simple_queue_destroy(&q, now);
}

void test_simple_stack() {
    ttak_simple_stack_t s;
    ttak_simple_stack_init(&s);
    uint64_t now = 1000;

    int a = 1, b = 2, c = 3;

    ttak_simple_stack_push(&s, &a, now);
    ttak_simple_stack_push(&s, &b, now);
    ttak_simple_stack_push(&s, &c, now);

    ASSERT(ttak_simple_stack_size(&s) == 3);

    void *p1 = ttak_simple_stack_pop(&s, now);
    ASSERT(p1 == &c);

    void *p2 = ttak_simple_stack_pop(&s, now);
    ASSERT(p2 == &b);

    void *p3 = ttak_simple_stack_pop(&s, now);
    ASSERT(p3 == &a);

    ASSERT(ttak_simple_stack_is_empty(&s));
    ttak_simple_stack_destroy(&s, now);
}

void test_nice_utils() {
    // Test lock_priority
    ASSERT(ttak_lock_priority(-20) == 0); 
    ASSERT(ttak_lock_priority(0) == 0);
    ASSERT(ttak_lock_priority(10) == 10);
    ASSERT(ttak_lock_priority(19) == 19);
    ASSERT(ttak_lock_priority(200) == 19);

    // Test shuffle_by_nice
    int nices[] = {1, 2, 3, 4, 5};
    ttak_shuffle_by_nice(nices, 5);
    
    // Check elements are still same set
    int sum = 0;
    for (int i=0; i<5; i++) sum += nices[i];
    ASSERT(sum == 15);
}

int main() {
    RUN_TEST(test_simple_queue);
    RUN_TEST(test_simple_stack);
    RUN_TEST(test_nice_utils);
    return 0;
}
