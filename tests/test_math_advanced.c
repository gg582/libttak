#include <ttak/math/bigreal.h>
#include <ttak/math/bigcomplex.h>
#include <ttak/mem/mem.h>
#include "test_macros.h"

void test_bigreal_init() {
    ttak_bigreal_t *br = ttak_mem_alloc(sizeof(ttak_bigreal_t), __TTAK_UNSAFE_MEM_FOREVER__, 100);
    ASSERT(br != NULL);
    ttak_bigreal_init(br, 100);
    ASSERT(br->exponent == 0);
    ttak_bigreal_free(br, 101);
    ttak_mem_free(br);
}

void test_bigcomplex_init() {
    ttak_bigcomplex_t *bc = ttak_mem_alloc(sizeof(ttak_bigcomplex_t), __TTAK_UNSAFE_MEM_FOREVER__, 200);
    ASSERT(bc != NULL);
    ttak_bigcomplex_init(bc, 200);
    ASSERT(bc->real.exponent == 0);
    ASSERT(bc->imag.exponent == 0);
    ttak_bigcomplex_free(bc, 201);
    ttak_mem_free(bc);
}

int main() {
    RUN_TEST(test_bigreal_init);
    RUN_TEST(test_bigcomplex_init);
    return 0;
}
