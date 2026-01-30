#ifndef TTAK_MATH_NTT_H
#define TTAK_MATH_NTT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if !defined(__SIZEOF_INT128__)
#error "ttak_ntt requires compiler support for __int128"
#endif

typedef unsigned __int128 ttak_uint128_native_t;

/**
 * @brief Predefined prime information for NTT operations.
 */
typedef struct ttak_ntt_prime {
    uint64_t modulus;        /**< Prime modulus suitable for 2^k NTT. */
    uint64_t primitive_root; /**< Primitive root of unity for the modulus. */
    uint32_t max_power_two;  /**< Maximum supported log2 transform size. */
    uint64_t montgomery_inv; /**< -mod^{-1} mod 2^64 for Montgomery reduction. */
    uint64_t montgomery_r2;  /**< R^2 mod modulus for Montgomery conversion. */
} ttak_ntt_prime_t;

#define TTAK_NTT_PRIME_COUNT 3

extern const ttak_ntt_prime_t ttak_ntt_primes[TTAK_NTT_PRIME_COUNT];

/**
 * @brief Portable 128-bit container for CRT outputs.
 */
typedef struct ttak_u128 {
    uint64_t lo;
    uint64_t hi;
} ttak_u128_t;

uint64_t ttak_mod_add(uint64_t a, uint64_t b, uint64_t mod);
uint64_t ttak_mod_sub(uint64_t a, uint64_t b, uint64_t mod);
uint64_t ttak_mod_mul(uint64_t a, uint64_t b, uint64_t mod);
uint64_t ttak_mod_pow(uint64_t base, uint64_t exp, uint64_t mod);
uint64_t ttak_mod_inverse(uint64_t value, uint64_t mod);

uint64_t ttak_montgomery_reduce(ttak_uint128_native_t value, const ttak_ntt_prime_t *prime);
uint64_t ttak_montgomery_mul(uint64_t lhs, uint64_t rhs, const ttak_ntt_prime_t *prime);
uint64_t ttak_montgomery_convert(uint64_t value, const ttak_ntt_prime_t *prime);

_Bool ttak_ntt_transform(uint64_t *data, size_t n, const ttak_ntt_prime_t *prime, _Bool inverse);
void ttak_ntt_pointwise_mul(uint64_t *dst, const uint64_t *lhs, const uint64_t *rhs, size_t n, const ttak_ntt_prime_t *prime);
void ttak_ntt_pointwise_square(uint64_t *dst, const uint64_t *src, size_t n, const ttak_ntt_prime_t *prime);

size_t ttak_next_power_of_two(size_t value);

typedef struct ttak_crt_term {
    uint64_t residue;
    uint64_t modulus;
} ttak_crt_term_t;

_Bool ttak_crt_combine(const ttak_crt_term_t *terms, size_t count, ttak_u128_t *residue_out, ttak_u128_t *modulus_out);

#endif // TTAK_MATH_NTT_H
