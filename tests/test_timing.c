#include <ttak/timing/timing.h>
#include "test_macros.h"
#include <unistd.h>

void test_timing_basic() {
    uint64_t t1 = ttak_get_tick_count();
    usleep(10000); // 10ms
    uint64_t t2 = ttak_get_tick_count();
    
    ASSERT(t1 > 0);
    ASSERT(t2 >= t1);
}

int main() {
    RUN_TEST(test_timing_basic);
    return 0;
}
