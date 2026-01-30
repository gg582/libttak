#include <ttak/sync/sync.h>
#include "test_macros.h"

void test_mutex_basic() {
    ttak_mutex_t mutex;
    ASSERT(ttak_mutex_init(&mutex) == 0);
    ASSERT(ttak_mutex_lock(&mutex) == 0);
    ASSERT(ttak_mutex_unlock(&mutex) == 0);
    ASSERT(ttak_mutex_destroy(&mutex) == 0);
}

int main() {
    RUN_TEST(test_mutex_basic);
    return 0;
}
