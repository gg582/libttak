#include <ttak/atomic/atomic.h>
#include "test_macros.h"

void test_atomic_basic() {
    uint64_t val = 10;
    
    ASSERT(ttak_atomic_read64(&val) == 10);
    
    ttak_atomic_write64(&val, 20);
    ASSERT(ttak_atomic_read64(&val) == 20);
    
    uint64_t next = ttak_atomic_inc64(&val);
    ASSERT(next == 21);
    ASSERT(ttak_atomic_read64(&val) == 21);
}

int main() {
    RUN_TEST(test_atomic_basic);
    return 0;
}