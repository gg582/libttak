#include <ttak/mem/mem.h>
#include "test_macros.h"
#include <string.h>

void test_mem_alloc_free() {
    uint64_t now = 100;
    void *ptr = ttak_mem_alloc(1024, 1000, now);
    ASSERT(ptr != NULL);
    
    void *accessed = ttak_mem_access(ptr, now + 500);
    ASSERT(accessed == ptr);
    
    // Test expiration
    void *expired = ttak_mem_access(ptr, now + 1500);
    ASSERT(expired == NULL);
    
    ttak_mem_free(ptr);
}

void test_mem_realloc() {
    uint64_t now = 200;
    void *ptr = ttak_mem_alloc(512, 1000, now);
    ASSERT(ptr != NULL);
    
    memset(ptr, 0xAA, 512);
    
    void *new_ptr = ttak_mem_realloc(ptr, 1024, 1000, now + 10);
    ASSERT(new_ptr != NULL);
    
    // Check if data was preserved
    unsigned char *cptr = (unsigned char *)new_ptr;
    for (int i = 0; i < 512; i++) {
        ASSERT(cptr[i] == 0xAA);
    }
    
    ttak_mem_free(new_ptr);
}

int main() {
    RUN_TEST(test_mem_alloc_free);
    RUN_TEST(test_mem_realloc);
    return 0;
}
