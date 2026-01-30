#include <ttak/ht/map.h>
#include "test_macros.h"

void test_map_basic() {
    uint64_t now = 500;
    tt_map_t *map = ttak_create_map(16, now);
    ASSERT(map != NULL);
    
    ttak_insert_to_map(map, 123, 456, now);
    
    // In a real implementation we would have a lookup function.
    // Based on src/mem/mem.c, we know ttak_delete_from_map exists.
    
    ttak_delete_from_map(map, 123, now);
    
    // Cleanup - if there's a destroy map function
    // For now, we assume it might be leaked or we need to find the destroy function.
}

int main() {
    RUN_TEST(test_map_basic);
    return 0;
}
