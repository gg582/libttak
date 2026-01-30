#include <ttak/math/bigreal.h>
#include <ttak/math/bigcomplex.h>
#include <ttak/math/ntt.h>
#include <ttak/mem/mem.h>
#include "test_macros.h"
#include <string.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static uint64_t bigint_as_u64(const ttak_bigint_t *bi) {
    const limb_t *limbs = bi->is_dynamic ? bi->data.dyn_ptr : bi->data.sso_buf;
    uint64_t value = 0;
    size_t max_limbs = bi->used < 2 ? bi->used : 2;
    for (size_t i = 0; i < max_limbs; ++i) {
        value |= ((uint64_t)limbs[i]) << (i * 32);
    }
    return value;
}

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

void test_bigreal_add_basic() {
    ttak_bigreal_t lhs, rhs, dst;
    ttak_bigreal_init(&lhs, 300);
    ttak_bigreal_init(&rhs, 301);
    ttak_bigreal_init(&dst, 302);

    lhs.exponent = rhs.exponent = 7;
    ASSERT(ttak_bigint_set_u64(&lhs.mantissa, 100, 303));
    ASSERT(ttak_bigint_set_u64(&rhs.mantissa, 50, 304));

    ASSERT(ttak_bigreal_add(&dst, &lhs, &rhs, 305));
    ASSERT(dst.exponent == 7);
    ASSERT(bigint_as_u64(&dst.mantissa) == 150);

    rhs.exponent = 8;
    ASSERT(!ttak_bigreal_add(&dst, &lhs, &rhs, 306));

    ttak_bigreal_free(&lhs, 307);
    ttak_bigreal_free(&rhs, 308);
    ttak_bigreal_free(&dst, 309);
}

void test_bigcomplex_add_basic() {
    ttak_bigcomplex_t lhs, rhs, dst;
    ttak_bigcomplex_init(&lhs, 400);
    ttak_bigcomplex_init(&rhs, 401);
    ttak_bigcomplex_init(&dst, 402);

    lhs.real.exponent = rhs.real.exponent = 1;
    lhs.imag.exponent = rhs.imag.exponent = 3;
    ASSERT(ttak_bigint_set_u64(&lhs.real.mantissa, 10, 403));
    ASSERT(ttak_bigint_set_u64(&rhs.real.mantissa, 20, 404));
    ASSERT(ttak_bigint_set_u64(&lhs.imag.mantissa, 5, 405));
    ASSERT(ttak_bigint_set_u64(&rhs.imag.mantissa, 15, 406));

    ASSERT(ttak_bigcomplex_add(&dst, &lhs, &rhs, 407));
    ASSERT(dst.real.exponent == 1);
    ASSERT(dst.imag.exponent == 3);
    ASSERT(bigint_as_u64(&dst.real.mantissa) == 30);
    ASSERT(bigint_as_u64(&dst.imag.mantissa) == 20);

    rhs.imag.exponent = 4;
    ASSERT(!ttak_bigcomplex_add(&dst, &lhs, &rhs, 408));

    ttak_bigcomplex_free(&lhs, 409);
    ttak_bigcomplex_free(&rhs, 410);
    ttak_bigcomplex_free(&dst, 411);
}

void test_ntt_roundtrip() {
    const ttak_ntt_prime_t *prime = &ttak_ntt_primes[0];
    uint64_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint64_t expected[8];
    memcpy(expected, data, sizeof(data));

    ASSERT(ttak_ntt_transform(data, ARRAY_SIZE(data), prime, false));
    ASSERT(ttak_ntt_transform(data, ARRAY_SIZE(data), prime, true));

    for (size_t i = 0; i < ARRAY_SIZE(data); ++i) {
        ASSERT(data[i] == expected[i] % prime->modulus);
    }
}

void test_ntt_pointwise_mul() {
    const ttak_ntt_prime_t *prime = &ttak_ntt_primes[1];
    size_t n = 4;
    uint64_t left[4] = {1, 2, 3, 4};
    uint64_t right[4] = {5, 6, 7, 8};
    uint64_t result[4];

    ASSERT(ttak_ntt_transform(left, n, prime, false));
    ASSERT(ttak_ntt_transform(right, n, prime, false));
    ttak_ntt_pointwise_mul(result, left, right, n, prime);
    ASSERT(ttak_ntt_transform(result, n, prime, true));

    uint64_t expected[4];
    for (size_t i = 0; i < n; ++i) expected[i] = 0;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            expected[(i + j) % n] = (expected[(i + j) % n] + (i + 1) * (j + 5)) % prime->modulus;
        }
    }

    for (size_t i = 0; i < n; ++i) {
        ASSERT(result[i] == expected[i]);
    }
}

void test_crt_combine_basic() {
    ttak_uint128_native_t value = ((ttak_uint128_native_t)1 << 96) + 0x123456789ULL;
    ttak_crt_term_t terms[2];
    terms[0].modulus = ttak_ntt_primes[0].modulus;
    terms[0].residue = (uint64_t)(value % terms[0].modulus);
    terms[1].modulus = ttak_ntt_primes[1].modulus;
    terms[1].residue = (uint64_t)(value % terms[1].modulus);

    ttak_u128_t result, modulus;
    ASSERT(ttak_crt_combine(terms, 2, &result, &modulus));

    ttak_uint128_native_t combined = ((ttak_uint128_native_t)result.hi << 64) | result.lo;
    ttak_uint128_native_t combined_mod = ((ttak_uint128_native_t)modulus.hi << 64) | modulus.lo;

    ASSERT((combined % terms[0].modulus) == terms[0].residue);
    ASSERT((combined % terms[1].modulus) == terms[1].residue);
    ASSERT(combined < combined_mod);
}

void test_next_power_of_two() {
    ASSERT(ttak_next_power_of_two(0) == 1);
    ASSERT(ttak_next_power_of_two(1) == 1);
    ASSERT(ttak_next_power_of_two(3) == 4);
    ASSERT(ttak_next_power_of_two(17) == 32);
}

int main() {
    RUN_TEST(test_bigreal_init);
    RUN_TEST(test_bigcomplex_init);
    RUN_TEST(test_bigreal_add_basic);
    RUN_TEST(test_bigcomplex_add_basic);
    RUN_TEST(test_ntt_roundtrip);
    RUN_TEST(test_ntt_pointwise_mul);
    RUN_TEST(test_crt_combine_basic);
    RUN_TEST(test_next_power_of_two);
    return 0;
}
