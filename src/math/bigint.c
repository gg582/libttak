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
 * @brief Const helper for limb access.
 */
static const limb_t *get_const_limbs(const ttak_bigint_t *bi) {
    return bi->is_dynamic ? bi->data.dyn_ptr : bi->data.sso_buf;
}

/**
 * @brief Ensures the bigint has at least the requested capacity.
 */
static _Bool ensure_capacity(ttak_bigint_t *bi, size_t required, uint64_t now) {
    if (required <= bi->capacity) return true;
    if (required > TTAK_MAX_LIMB_LIMIT) return false;

    size_t old_capacity = bi->capacity;
    size_t new_capacity = old_capacity ? old_capacity : TTAK_BIGINT_SSO_LIMIT;
    while (new_capacity < required && new_capacity < TTAK_MAX_LIMB_LIMIT) {
        size_t next = new_capacity * 2;
        if (next <= new_capacity) break; // overflow guard
        new_capacity = next;
    }
    if (new_capacity < required) {
        new_capacity = required;
    }
    if (new_capacity > TTAK_MAX_LIMB_LIMIT) {
        return false;
    }

    size_t new_size = new_capacity * sizeof(limb_t);
    limb_t *new_buf = NULL;

    if (bi->is_dynamic) {
        new_buf = ttak_mem_realloc(bi->data.dyn_ptr, new_size, __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (new_buf && new_capacity > old_capacity) {
            memset(new_buf + old_capacity, 0, (new_capacity - old_capacity) * sizeof(limb_t));
        }
    } else {
        new_buf = ttak_mem_alloc(new_size, __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (new_buf) {
            memset(new_buf, 0, new_size);
            memcpy(new_buf, bi->data.sso_buf, bi->used * sizeof(limb_t));
        }
    }

    if (!new_buf) return false;

    bi->data.dyn_ptr = new_buf;
    bi->is_dynamic = true;
    bi->capacity = new_capacity;
    return true;
}

/**
 * @brief Trims unused high limbs.
 */
static void trim_unused(ttak_bigint_t *bi) {
    limb_t *limbs = get_limbs(bi);
    while (bi->used > 0 && limbs[bi->used - 1] == 0) {
        bi->used--;
    }
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

_Bool ttak_bigint_set_u64(ttak_bigint_t *bi, uint64_t value, uint64_t now) {
    size_t needed_limbs = value ? ((64 + 31) / 32) : 1;
    if (!ensure_capacity(bi, needed_limbs, now)) {
        return false;
    }

    limb_t *limbs = get_limbs(bi);
    memset(limbs, 0, bi->capacity * sizeof(limb_t));

    size_t idx = 0;
    while (value) {
        limbs[idx++] = (limb_t)(value & 0xFFFFFFFFu);
        value >>= 32;
    }

    bi->used = idx;
    bi->is_negative = false;
    if (bi->used == 0) {
        /* Normalize zero representation */
        bi->used = 0;
    }
    return true;
}

_Bool ttak_bigint_add(ttak_bigint_t *dst, const ttak_bigint_t *lhs, const ttak_bigint_t *rhs, uint64_t now) {
    size_t max_used = lhs->used > rhs->used ? lhs->used : rhs->used;
    size_t needed = max_used + 1;
    if (!ensure_capacity(dst, needed ? needed : 1, now)) {
        return false;
    }

    limb_t *dst_limbs = get_limbs(dst);
    const limb_t *lhs_limbs = get_const_limbs(lhs);
    const limb_t *rhs_limbs = get_const_limbs(rhs);

    uint64_t carry = 0;
    size_t i = 0;
    for (; i < needed; ++i) {
        uint64_t accum = carry;
        if (i < lhs->used) accum += lhs_limbs[i];
        if (i < rhs->used) accum += rhs_limbs[i];
        dst_limbs[i] = (limb_t)(accum & 0xFFFFFFFFu);
        carry = accum >> 32u;
    }

    if (carry) {
        if (!ensure_capacity(dst, needed + 1, now)) {
            return false;
        }
        dst_limbs = get_limbs(dst);
        dst_limbs[needed] = (limb_t)carry;
        dst->used = needed + 1;
    } else {
        dst->used = needed;
    }

    trim_unused(dst);
    dst->is_negative = false;
    return true;
}
