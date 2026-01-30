#include <ttak/math/bigint.h>
#include <ttak/math/bigmul.h>
#include <ttak/mem/mem.h>
#include "test_macros.h"
#include <string.h>

static uint64_t bigint_as_u64(const ttak_bigint_t *bi) {
    const limb_t *limbs = bi->is_dynamic ? bi->data.dyn_ptr : bi->data.sso_buf;
    uint64_t value = 0;
    size_t max_limbs = bi->used < 2 ? bi->used : 2;
    for (size_t i = 0; i < max_limbs; ++i) {
        value |= ((uint64_t)limbs[i]) << (i * 32);
    }
    return value;
}

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

void test_bigmul_init() {
    tt_big_mul_t *bm = ttak_mem_alloc(sizeof(tt_big_mul_t), __TTAK_UNSAFE_MEM_FOREVER__, 4);
    ASSERT(bm != NULL);

    ttak_bigmul_init(bm, 4);

    ASSERT(bm->lhs.capacity == TTAK_BIGINT_SSO_LIMIT);
    ASSERT(bm->rhs.capacity == TTAK_BIGINT_SSO_LIMIT);
    ASSERT(bm->product.capacity == TTAK_BIGINT_SSO_LIMIT);

    ttak_bigmul_free(bm, 5);
    ttak_mem_free(bm);
}

void test_bigint_unit_ops() {
    ttak_bigint_t a, b, sum;
    ttak_bigint_init(&a, 10);
    ttak_bigint_init(&b, 11);
    ttak_bigint_init(&sum, 12);

    ASSERT(ttak_bigint_set_u64(&a, (1ULL << 33) + 7, 13));
    ASSERT(ttak_bigint_set_u64(&b, 5, 14));
    ASSERT(ttak_bigint_add(&sum, &a, &b, 15));

    ASSERT(bigint_as_u64(&sum) == ((1ULL << 33) + 12));

    ttak_bigint_free(&a, 16);
    ttak_bigint_free(&b, 17);
    ttak_bigint_free(&sum, 18);
}

int main() {
    RUN_TEST(test_bigint_init);
    RUN_TEST(test_bigint_mersenne_mod_basic);
    RUN_TEST(test_bigmul_init);
    RUN_TEST(test_bigint_unit_ops);
    return 0;
}
