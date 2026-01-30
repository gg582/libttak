#ifndef TTAK_MATH_BIGINT_H
#define TTAK_MATH_BIGINT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdalign.h>

/**
 * @brief Platform-optimized word type for Raspberry Pi (ARMv7/v8).
 */
typedef uint32_t limb_t;

#define TTAK_BIGINT_SSO_LIMIT 4 // 4 * 32-bit = 128-bit

/**
 * @brief Arbitrary-precision integer engine with Small Stack Optimization (SSO).
 */
typedef struct ttak_bigint {
    size_t  capacity;
    size_t  used;
    _Bool   is_negative;
    _Bool   is_dynamic;
    union {
        limb_t *dyn_ptr;
        limb_t sso_buf[TTAK_BIGINT_SSO_LIMIT];
    } data;
    alignas(max_align_t) char padding[0];
} ttak_bigint_t;

typedef ttak_bigint_t __tt_big_i_t;

/**
 * @brief Initializes a bigint.
 */
void ttak_bigint_init(ttak_bigint_t *bi, uint64_t now);

/**
 * @brief Performs modulo operation optimized for Mersenne primes (2^p - 1).
 */
void ttak_bigint_mersenne_mod(ttak_bigint_t *bi, int p, uint64_t now);

/**
 * @brief Frees the bigint resources.
 */
void ttak_bigint_free(ttak_bigint_t *bi, uint64_t now);

/**
 * @brief Assigns an unsigned 64-bit value to the bigint.
 *
 * @return true on success, false on allocation failure.
 */
_Bool ttak_bigint_set_u64(ttak_bigint_t *bi, uint64_t value, uint64_t now);

/**
 * @brief Adds two non-negative bigints: dst = lhs + rhs.
 *
 * The destination must be initialized. Sign handling is limited to positive inputs.
 *
 * @return true on success, false on allocation failure.
 */
_Bool ttak_bigint_add(ttak_bigint_t *dst, const ttak_bigint_t *lhs, const ttak_bigint_t *rhs, uint64_t now);

#endif // TTAK_MATH_BIGINT_H
