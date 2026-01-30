#include <ttak/priority/nice.h>
#include "test_macros.h"

void test_nice_basic() {
    ASSERT(ttak_nice_to_prio(0) == 0);
    ASSERT(ttak_nice_to_prio(-20) == -20);
    ASSERT(ttak_nice_to_prio(19) == 19);
    
    // Bounds check if implemented
    // In src/priority/nice.c we'll see if it clamps.
}

int main() {
    RUN_TEST(test_nice_basic);
    return 0;
}
