#include <ttak/math/bigint.h>
#include <ttak/mem/mem.h>
#include "test_macros.h"
#include <string.h>

void test_bigint_init() {
    ttak_bigint_t *bi = ttak_mem_alloc(sizeof(ttak_bigint_t), __TTAK_UNSAFE_MEM_FOREVER__, 0);
    ASSERT(bi != NULL);
    ttak_bigint_init(bi, 0);
    
    ASSERT(bi->capacity == TTAK_BIGINT_SSO_LIMIT);
    ASSERT(bi->used == 0);
    ASSERT(bi->is_negative == false);
    ASSERT(bi->is_dynamic == false);
    
    ttak_bigint_free(bi, 1);
    ttak_mem_free(bi);
}

void test_bigint_mersenne_mod_basic() {
    ttak_bigint_t *bi = ttak_mem_alloc(sizeof(ttak_bigint_t), __TTAK_UNSAFE_MEM_FOREVER__, 0);
    ASSERT(bi != NULL);
    ttak_bigint_init(bi, 0);
    
    ttak_bigint_mersenne_mod(bi, 31, 2);
    
    ttak_bigint_free(bi, 3);
    ttak_mem_free(bi);
}

int main() {
    RUN_TEST(test_bigint_init);
    RUN_TEST(test_bigint_mersenne_mod_basic);
    return 0;
}
