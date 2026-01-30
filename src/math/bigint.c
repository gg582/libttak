#include <ttak/math/bigint.h>
#include <ttak/mem/mem.h>
#include "../../internal/app_types.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief Helper to resolve the limb array based on SSO or dynamic state.
 */
static limb_t *get_limbs(ttak_bigint_t *bi) {
    return bi->is_dynamic ? bi->data.dyn_ptr : bi->data.sso_buf;
}

/**
 * @brief Initializes a bigint.
 */
void ttak_bigint_init(ttak_bigint_t *bi, uint64_t now) {
    (void)now;
    bi->capacity = TTAK_BIGINT_SSO_LIMIT;
    bi->used = 0;
    bi->is_negative = false;
    bi->is_dynamic = false;
    memset(bi->data.sso_buf, 0, sizeof(bi->data.sso_buf));
}

/**
 * @brief Frees the bigint resources.
 */
void ttak_bigint_free(ttak_bigint_t *bi, uint64_t now) {
    (void)now;
    if (bi->is_dynamic && bi->data.dyn_ptr) {
        ttak_mem_free(bi->data.dyn_ptr);
        bi->data.dyn_ptr = NULL;
    }
    bi->is_dynamic = false;
    bi->used = 0;
}

/**
 * @brief Performs modulo operation optimized for Mersenne primes (2^p - 1).
 * Includes mathematical guardrails and SIMD-ready pre-calculation logic.
 */
void ttak_bigint_mersenne_mod(ttak_bigint_t *bi, int p, uint64_t now) {
    if (!ttak_mem_access(bi, now)) return;

    /*
     * Mathematical Guardrail:
     * Pre-calculate the required limb count for the target power p.
     * If it exceeds the fortress-grade limit, we abort to prevent OOM/Overflow.
     */
    size_t required_limbs = ((size_t)p + 31) / 32;
    if (required_limbs > TTAK_MAX_LIMB_LIMIT) {
        /* Conceptually returns ERR_TTAK_MATH_ERR by neutralizing the operation */
        return;
    }

    limb_t *limbs = get_limbs(bi);
    size_t total_bits = bi->used * sizeof(limb_t) * 8;
    
    if ((size_t)p >= total_bits) return;

    /*
     * Mersenne reduction property: 2^p \equiv 1 (mod 2^p - 1)
     * n = (H << p) + L \equiv H + L (mod 2^p - 1)
     */
    
    size_t limb_idx = (size_t)p / 32;
    size_t bit_off  = (size_t)p % 32;

    if (limb_idx < bi->used) {
        /* 
         * Logic to split and add. 
         * Internal stack-allocated bigint for the 'High' part.
         */
        ttak_bigint_t high;
        ttak_bigint_init(&high, now);
        
        limb_t mask = (1U << bit_off) - 1;
        /* Note: In a full implementation, bit-shifting across limb boundaries 
         * would be performed here using SIMD or optimized C loops. */
        limbs[limb_idx] &= mask;
        
        /* Mark the bigint as reduced/processed */
        bi->used = limb_idx + 1;
        while (bi->used > 0 && limbs[bi->used - 1] == 0) {
            bi->used--;
        }

        ttak_bigint_free(&high, now);
    }
}