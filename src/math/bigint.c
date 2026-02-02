#include <ttak/math/bigint.h>
#include <ttak/mem/mem.h>
#include "../../internal/app_types.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static limb_t *get_limbs(ttak_bigint_t *bi) {
    return bi->is_dynamic ? bi->data.dyn_ptr : bi->data.sso_buf;
}

static const limb_t *get_const_limbs(const ttak_bigint_t *bi) {
    return bi->is_dynamic ? bi->data.dyn_ptr : bi->data.sso_buf;
}

static _Bool ensure_capacity(ttak_bigint_t *bi, size_t required, uint64_t now) {
    if (required <= bi->capacity) return true;
    if (required > TTAK_MAX_LIMB_LIMIT) return false;

    size_t old_capacity = bi->capacity;
    size_t new_capacity = old_capacity ? old_capacity : TTAK_BIGINT_SSO_LIMIT;
    while (new_capacity < required && new_capacity < TTAK_MAX_LIMB_LIMIT) {
        size_t next = new_capacity * 2;
        if (next <= new_capacity) break;
        new_capacity = next;
    }
    if (new_capacity < required) new_capacity = required;
    if (new_capacity > TTAK_MAX_LIMB_LIMIT) return false;

    size_t new_size = new_capacity * sizeof(limb_t);
    limb_t *new_buf = NULL;

    if (bi->is_dynamic) {
        new_buf = ttak_mem_realloc(bi->data.dyn_ptr, new_size, __TTAK_UNSAFE_MEM_FOREVER__, now);
    } else {
        new_buf = ttak_mem_alloc(new_size, __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (new_buf) {
            memcpy(new_buf, bi->data.sso_buf, bi->used * sizeof(limb_t));
        }
    }

    if (!new_buf) return false;
    if (new_capacity > old_capacity) {
        memset(new_buf + old_capacity, 0, (new_capacity - old_capacity) * sizeof(limb_t));
    }

    bi->data.dyn_ptr = new_buf;
    bi->is_dynamic = true;
    bi->capacity = new_capacity;
    return true;
}

static void trim_unused(ttak_bigint_t *bi) {
    limb_t *limbs = get_limbs(bi);
    while (bi->used > 0 && limbs[bi->used - 1] == 0) {
        bi->used--;
    }
    if (bi->used == 0) {
        bi->is_negative = false;
    }
}

void ttak_bigint_init(ttak_bigint_t *bi, uint64_t now) {
    (void)now;
    bi->capacity = TTAK_BIGINT_SSO_LIMIT;
    bi->used = 0;
    bi->is_negative = false;
    bi->is_dynamic = false;
    memset(bi->data.sso_buf, 0, sizeof(bi->data.sso_buf));
}

void ttak_bigint_init_u64(ttak_bigint_t *bi, uint64_t value, uint64_t now) {
    ttak_bigint_init(bi, now);
    ttak_bigint_set_u64(bi, value, now);
}

void ttak_bigint_init_copy(ttak_bigint_t *dst, const ttak_bigint_t *src, uint64_t now) {
    ttak_bigint_init(dst, now);
    ttak_bigint_copy(dst, src, now);
}

void ttak_bigint_free(ttak_bigint_t *bi, uint64_t now) {
    (void)now;
    if (bi && bi->is_dynamic && bi->data.dyn_ptr) {
        ttak_mem_free(bi->data.dyn_ptr);
    }
    if (bi) {
      memset(bi, 0, sizeof(*bi));
    }
}

_Bool ttak_bigint_set_u64(ttak_bigint_t *bi, uint64_t value, uint64_t now) {
    bi->is_negative = false;
    if (value == 0) {
        bi->used = 0;
        return true;
    }

    size_t needed_limbs = (value > 0xFFFFFFFFu) ? 2 : 1;
    if (!ensure_capacity(bi, needed_limbs, now)) {
        return false;
    }

    limb_t *limbs = get_limbs(bi);
    if (needed_limbs == 1) {
        limbs[0] = (limb_t)value;
        bi->used = 1;
    } else {
        limbs[0] = (limb_t)(value & 0xFFFFFFFFu);
        limbs[1] = (limb_t)(value >> 32);
        bi->used = 2;
    }
    return true;
}

_Bool ttak_bigint_copy(ttak_bigint_t *dst, const ttak_bigint_t *src, uint64_t now) {
    if (dst == src) return true;
    if (!ensure_capacity(dst, src->used, now)) return false;
    
    dst->used = src->used;
    dst->is_negative = src->is_negative;
    memcpy(get_limbs(dst), get_const_limbs(src), src->used * sizeof(limb_t));
    return true;
}

int ttak_bigint_cmp(const ttak_bigint_t *lhs, const ttak_bigint_t *rhs) {
    if (lhs->is_negative != rhs->is_negative) {
        return lhs->is_negative ? -1 : 1;
    }
    
    int sign = lhs->is_negative ? -1 : 1;

    if (lhs->used != rhs->used) {
        return (lhs->used > rhs->used) ? sign : -sign;
    }

    const limb_t *l = get_const_limbs(lhs);
    const limb_t *r = get_const_limbs(rhs);

    for (size_t i = lhs->used; i > 0; --i) {
        if (l[i-1] != r[i-1]) {
            return (l[i-1] > r[i-1]) ? sign : -sign;
        }
    }
    return 0;
}

int ttak_bigint_cmp_u64(const ttak_bigint_t *lhs, uint64_t rhs) {
    ttak_bigint_t rhs_bi;
    ttak_bigint_init_u64(&rhs_bi, rhs, 0);
    int result = ttak_bigint_cmp(lhs, &rhs_bi);
    ttak_bigint_free(&rhs_bi, 0);
    return result;
}

bool ttak_bigint_is_zero(const ttak_bigint_t *bi) {
    return bi->used == 0 || (bi->used == 1 && get_const_limbs(bi)[0] == 0);
}

_Bool ttak_bigint_add(ttak_bigint_t *dst, const ttak_bigint_t *lhs, const ttak_bigint_t *rhs, uint64_t now) {
    if (lhs->is_negative != rhs->is_negative) {
        // Subtraction case
        if (lhs->is_negative) { // (-a) + b == b - a
            return ttak_bigint_sub(dst, rhs, lhs, now);
        } else { // a + (-b) == a - b
            return ttak_bigint_sub(dst, lhs, rhs, now);
        }
    }

    size_t max_used = lhs->used > rhs->used ? lhs->used : rhs->used;
    if (!ensure_capacity(dst, max_used + 1, now)) return false;

    limb_t *d = get_limbs(dst);
    const limb_t *l = get_const_limbs(lhs);
    const limb_t *r = get_const_limbs(rhs);

    uint64_t carry = 0;
    size_t i = 0;
    for (; i < max_used; ++i) {
        uint64_t sum = carry;
        if (i < lhs->used) sum += l[i];
        if (i < rhs->used) sum += r[i];
        d[i] = (limb_t)sum;
        carry = sum >> 32;
    }
    if (carry) {
        d[i++] = (limb_t)carry;
    }
    dst->used = i;
    dst->is_negative = lhs->is_negative;
    trim_unused(dst);
    return true;
}

_Bool ttak_bigint_sub(ttak_bigint_t *dst, const ttak_bigint_t *lhs, const ttak_bigint_t *rhs, uint64_t now) {
    if (lhs->is_negative != rhs->is_negative) {
        // Addition case
        if (lhs->is_negative) { // (-a) - b == -(a + b)
            _Bool ok = ttak_bigint_add(dst, lhs, rhs, now);
            if (ok) dst->is_negative = true;
            return ok;
        } else { // a - (-b) == a + b
            return ttak_bigint_add(dst, lhs, rhs, now);
        }
    }

    // Same signs: a - b or (-a) - (-b) == b - a
    const ttak_bigint_t *a = lhs, *b = rhs;
    if (lhs->is_negative) { // Switch for (-a) - (-b)
        a = rhs; b = lhs;
    }

    int cmp = ttak_bigint_cmp(a, b);
    if (cmp == 0) {
        return ttak_bigint_set_u64(dst, 0, now);
    }

    bool result_is_negative = (cmp < 0);
    if (result_is_negative) {
        const ttak_bigint_t *tmp = a; a = b; b = tmp;
    }

    if (!ensure_capacity(dst, a->used, now)) return false;

    limb_t *d = get_limbs(dst);
    const limb_t *l = get_const_limbs(a);
    const limb_t *r = get_const_limbs(b);

    uint64_t borrow = 0;
    size_t i = 0;
    for (; i < a->used; ++i) {
        uint64_t diff = (uint64_t)l[i] - borrow;
        if (i < b->used) diff -= r[i];
        d[i] = (limb_t)diff;
        borrow = (diff >> 32) & 1;
    }
    dst->used = i;
    dst->is_negative = result_is_negative;
    trim_unused(dst);
    return true;
}

_Bool ttak_bigint_mul(ttak_bigint_t *dst, const ttak_bigint_t *lhs, const ttak_bigint_t *rhs, uint64_t now) {
    if (ttak_bigint_is_zero(lhs) || ttak_bigint_is_zero(rhs)) {
        return ttak_bigint_set_u64(dst, 0, now);
    }

    size_t needed = lhs->used + rhs->used;
    ttak_bigint_t tmp;
    ttak_bigint_init(&tmp, now);
    if (!ensure_capacity(&tmp, needed, now)) return false;

    limb_t *t = get_limbs(&tmp);
    const limb_t *l = get_const_limbs(lhs);
    const limb_t *r = get_const_limbs(rhs);

    for (size_t i = 0; i < lhs->used; ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < rhs->used; ++j) {
            uint64_t prod = (uint64_t)l[i] * r[j] + t[i+j] + carry;
            t[i+j] = (limb_t)prod;
            carry = prod >> 32;
        }
        if (carry) {
            t[i + rhs->used] += (limb_t)carry;
        }
    }

    tmp.used = needed;
    tmp.is_negative = lhs->is_negative != rhs->is_negative;
    trim_unused(&tmp);
    
    _Bool ok = ttak_bigint_copy(dst, &tmp, now);
    ttak_bigint_free(&tmp, now);
    return ok;
}

_Bool ttak_bigint_div_u64(ttak_bigint_t *q, ttak_bigint_t *r, const ttak_bigint_t *n, uint64_t d, uint64_t now) {
    if (d == 0) return false; // Division by zero

    // Handle zero dividend
    if (ttak_bigint_is_zero(n)) {
        if (q) ttak_bigint_set_u64(q, 0, now);
        if (r) ttak_bigint_set_u64(r, 0, now);
        return true;
    }

    // Handle division by 1
    if (d == 1) {
        if (q) ttak_bigint_copy(q, n, now);
        if (r) ttak_bigint_set_u64(r, 0, now);
        return true;
    }

    // Determine signs
    bool q_neg = n->is_negative; // d is always positive (uint64_t)
    bool r_neg = n->is_negative;

    // Use absolute value of n for calculation
    ttak_bigint_t n_abs;
    ttak_bigint_init_copy(&n_abs, n, now);
    n_abs.is_negative = false;

    // If dividend is smaller than divisor, quotient is 0, remainder is dividend
    if (ttak_bigint_cmp_u64(&n_abs, d) < 0) {
        if (q) ttak_bigint_set_u64(q, 0, now);
        if (r) ttak_bigint_copy(r, n, now); // Remainder keeps original sign
        ttak_bigint_free(&n_abs, now);
        return true;
    }

    // Prepare quotient (q)
    if (q) {
        if (!ensure_capacity(q, n_abs.used, now)) {
            ttak_bigint_free(&n_abs, now);
            return false;
        }
        q->used = n_abs.used;
        memset(get_limbs(q), 0, q->capacity * sizeof(limb_t));
    }
    limb_t *q_limbs = q ? get_limbs(q) : NULL;
    const limb_t *n_limbs = get_const_limbs(&n_abs);

    uint64_t remainder = 0; // Current remainder for long division
    for (size_t i = n_abs.used; i > 0; --i) {
        remainder = (remainder << 32) | n_limbs[i-1];
        if (q_limbs) {
            q_limbs[i-1] = (limb_t)(remainder / d);
        }
        remainder %= d;
    }

    // Set quotient and remainder
    if (q) {
        q->is_negative = q_neg;
        trim_unused(q);
    }
    if (r) {
        ttak_bigint_set_u64(r, remainder, now);
        r->is_negative = r_neg;
    }
    
ttak_bigint_free(&n_abs, now);
    return true;
}

_Bool ttak_bigint_mod_u64(ttak_bigint_t *r, const ttak_bigint_t *n, uint64_t d, uint64_t now) {
    return ttak_bigint_div_u64(NULL, r, n, d, now);
}

_Bool ttak_bigint_add_u64(ttak_bigint_t *dst, const ttak_bigint_t *lhs, uint64_t rhs, uint64_t now) {
    ttak_bigint_t rhs_bi;
    ttak_bigint_init_u64(&rhs_bi, rhs, now);
    _Bool ok = ttak_bigint_add(dst, lhs, &rhs_bi, now);
    ttak_bigint_free(&rhs_bi, now);
    return ok;
}

_Bool ttak_bigint_mul_u64(ttak_bigint_t *dst, const ttak_bigint_t *lhs, uint64_t rhs, uint64_t now) {
    if (rhs == 0 || ttak_bigint_is_zero(lhs)) {
        return ttak_bigint_set_u64(dst, 0, now);
    }
    if (rhs == 1) {
        return ttak_bigint_copy(dst, lhs, now);
    }

    size_t rhs_used = (rhs > 0xFFFFFFFFu) ? 2 : 1;
    size_t needed = lhs->used + rhs_used;
    if (!ensure_capacity(dst, needed, now)) return false;

    limb_t *d = get_limbs(dst);
    const limb_t *l = get_const_limbs(lhs);
    
    limb_t rhs_limbs[2] = {(limb_t)rhs, (limb_t)(rhs >> 32)};

    memset(d, 0, dst->capacity * sizeof(limb_t));

    for (size_t i = 0; i < lhs->used; ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < rhs_used; ++j) {
            uint64_t prod = (uint64_t)l[i] * rhs_limbs[j] + d[i+j] + carry;
            d[i+j] = (limb_t)prod;
            carry = prod >> 32;
        }
        if (carry) {
            d[i + rhs_used] += (limb_t)carry;
        }
    }

    dst->used = needed;
    dst->is_negative = lhs->is_negative; // rhs is not negative
    trim_unused(dst);
    return true;
}

size_t ttak_bigint_get_bit_length(const ttak_bigint_t *bi) {
    if (ttak_bigint_is_zero(bi)) return 0;
    const limb_t *limbs = get_const_limbs(bi);
    size_t top_limb_idx = bi->used - 1;
    limb_t top_limb = limbs[top_limb_idx];
    size_t bits = top_limb_idx * 32;
    
    unsigned long msb_pos;
    #if defined(__GNUC__) || defined(__clang__)
        msb_pos = 31 - __builtin_clz(top_limb);
    #else
        msb_pos = 0;
        while ((1UL << msb_pos) <= top_limb && msb_pos < 32) {
            msb_pos++;
        }
        msb_pos--;
    #endif
    
    return bits + msb_pos + 1;
}

#define TTAK_BIGINT_BASE_BITS 32
#define TTAK_BIGINT_BASE (1ULL << TTAK_BIGINT_BASE_BITS)

// Helper for Knuth division: u -= v
limb_t sub_limbs(limb_t *u, const limb_t *v, size_t n) {
    uint64_t borrow = 0;
    for (size_t i = 0; i < n; ++i) {
        uint64_t diff = (uint64_t)u[i] - v[i] - borrow;
        u[i] = (limb_t)diff;
        borrow = (diff >> TTAK_BIGINT_BASE_BITS) & 1;
    }
    return (limb_t)borrow;
}

// Helper for Knuth division: u += v
limb_t add_limbs(limb_t *u, const limb_t *v, size_t n) {
    uint64_t carry = 0;
    for (size_t i = 0; i < n; ++i) {
        uint64_t sum = (uint64_t)u[i] + v[i] + carry;
        u[i] = (limb_t)sum;
        carry = sum >> TTAK_BIGINT_BASE_BITS;
    }
    return (limb_t)carry;
}

// Helper for Knuth division: left shift
static limb_t lshift_limbs(limb_t *num, size_t len, int shift_bits) {
    if (shift_bits == 0) return 0;
    limb_t carry = 0;
    for (size_t i = 0; i < len; ++i) {
        limb_t next_carry = num[i] >> (TTAK_BIGINT_BASE_BITS - shift_bits);
        num[i] = (num[i] << shift_bits) | carry;
        carry = next_carry;
    }
    return carry;
}

// Helper for Knuth division: right shift
static void rshift_limbs(limb_t *num, size_t len, int shift_bits) {
    if (shift_bits == 0) return;
    limb_t carry = 0;
    for (size_t i = len; i > 0; --i) {
        limb_t next_carry = num[i-1] << (TTAK_BIGINT_BASE_BITS - shift_bits);
        num[i-1] = (num[i-1] >> shift_bits) | carry;
        carry = next_carry;
    }
}

// Knuth's Algorithm D for division.
// q_out will have m + 1 limbs, r_out will have n limbs.
// u is the dividend (m+n limbs), v is the divisor (n limbs).
static _Bool knuth_div_limbs(limb_t *q_out, limb_t *r_out,
                             const limb_t *u, size_t m,
                             const limb_t *v, size_t n,
                             uint64_t now) {
    // D1. Normalize.
    int d = 0;
    if (n > 0) {
        limb_t vn_1 = v[n - 1];
        if (vn_1 == 0) return false; // Should not happen if d is normalized
        while (vn_1 < (limb_t)(TTAK_BIGINT_BASE / 2)) {
            vn_1 <<= 1;
            d++;
        }
    }

    limb_t *u_norm = ttak_mem_alloc((m + n + 1) * sizeof(limb_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    limb_t *v_norm = ttak_mem_alloc(n * sizeof(limb_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!u_norm || !v_norm) {
        ttak_mem_free(u_norm);
        ttak_mem_free(v_norm);
        return false;
    }
    memset(u_norm, 0, (m + n + 1) * sizeof(limb_t));

    memcpy(u_norm, u, (m + n) * sizeof(limb_t));
    if (d > 0) u_norm[m + n] = lshift_limbs(u_norm, m + n, d);

    memcpy(v_norm, v, n * sizeof(limb_t));
    if (d > 0) lshift_limbs(v_norm, n, d);

    // D2. Initialize j.
    for (int j = m; j >= 0; --j) {
        // D3. Calculate q_hat.
        uint64_t u_hat = ((uint64_t)u_norm[j + n] << TTAK_BIGINT_BASE_BITS) | u_norm[j + n - 1];
        limb_t q_hat;

        if (u_norm[j + n] == v_norm[n - 1]) {
            q_hat = (limb_t)-1;
        } else {
            q_hat = u_hat / v_norm[n - 1];
        }

        if (n > 1) {
            while ((uint64_t)q_hat * v_norm[n - 2] >
                   (u_hat - (uint64_t)q_hat * v_norm[n - 1]) * TTAK_BIGINT_BASE + u_norm[j + n - 2]) {
                q_hat--;
            }
        }

        // D4. Multiply and subtract.
        limb_t *u_norm_slice = &u_norm[j];
        uint64_t borrow = 0;
        uint64_t carry_prod = 0;
        for (size_t i = 0; i < n; ++i) {
            uint64_t prod = (uint64_t)v_norm[i] * q_hat + carry_prod;
            carry_prod = prod >> TTAK_BIGINT_BASE_BITS;
            uint64_t diff = (uint64_t)u_norm_slice[i] - (limb_t)prod - borrow;
            u_norm_slice[i] = (limb_t)diff;
            borrow = (diff >> TTAK_BIGINT_BASE_BITS) & 1;
        }
        uint64_t diff = (uint64_t)u_norm_slice[n] - carry_prod - borrow;
        u_norm_slice[n] = (limb_t)diff;
        borrow = (diff >> TTAK_BIGINT_BASE_BITS) & 1;

        // D5. Add back if remainder is negative.
        if (borrow > 0) {
            q_hat--;
            u_norm_slice[n] += add_limbs(u_norm_slice, v_norm, n);
        }

        // D6. Set quotient digit.
        if (q_out) q_out[j] = q_hat;
    }

    // D7. Unnormalize remainder.
    if (r_out) {
        memcpy(r_out, u_norm, n * sizeof(limb_t));
        if (d > 0) rshift_limbs(r_out, n, d);
    }

    ttak_mem_free(u_norm);
    ttak_mem_free(v_norm);
    return true;
}

_Bool ttak_bigint_div(ttak_bigint_t *q, ttak_bigint_t *r, const ttak_bigint_t *n, const ttak_bigint_t *d, uint64_t now) {
    if (ttak_bigint_is_zero(d)) return false;

    // Use absolute values for comparison and division
    ttak_bigint_t n_abs, d_abs;
    ttak_bigint_init_copy(&n_abs, n, now);
    ttak_bigint_init_copy(&d_abs, d, now);
    n_abs.is_negative = false;
    d_abs.is_negative = false;

    int cmp = ttak_bigint_cmp(&n_abs, &d_abs);

    ttak_bigint_free(&n_abs, now);
    ttak_bigint_free(&d_abs, now);

    if (cmp < 0) {
        if (q) ttak_bigint_set_u64(q, 0, now);
        if (r) ttak_bigint_copy(r, n, now);
        return true;
    }
    if (cmp == 0) {
        if (q) {
            ttak_bigint_set_u64(q, 1, now);
            q->is_negative = n->is_negative != d->is_negative;
        }
        if (r) ttak_bigint_set_u64(r, 0, now);
        return true;
    }

    ttak_bigint_t n_tmp;
    ttak_bigint_init_copy(&n_tmp, n, now);
    n_tmp.is_negative = false;

    ttak_bigint_t d_tmp;
    ttak_bigint_init_copy(&d_tmp, d, now);
    d_tmp.is_negative = false;

    const limb_t *n_limbs = get_const_limbs(&n_tmp);
    const limb_t *d_limbs = get_const_limbs(&d_tmp);
    size_t n_used = n_tmp.used;
    size_t d_used = d_tmp.used;

    size_t m = n_used - d_used;
    size_t q_limbs_len = m + 1;

    limb_t *q_limbs = NULL;
    if (q) {
        if (!ensure_capacity(q, q_limbs_len, now)) {
             ttak_bigint_free(&n_tmp, now);
             ttak_bigint_free(&d_tmp, now);
             return false;
        }
        q_limbs = get_limbs(q);
        memset(q_limbs, 0, q->capacity * sizeof(limb_t));
    }

    limb_t *r_limbs = NULL;
    if (r) {
        if (!ensure_capacity(r, d_used, now)) {
            ttak_bigint_free(&n_tmp, now);
            ttak_bigint_free(&d_tmp, now);
            return false;
        }
        r_limbs = get_limbs(r);
        memset(r_limbs, 0, r->capacity * sizeof(limb_t));
    }

    _Bool ok = knuth_div_limbs(q_limbs, r_limbs, n_limbs, m, d_limbs, d_used, now);

    if (ok) {
        if (q) {
            q->used = q_limbs_len;
            q->is_negative = n->is_negative != d->is_negative;
            trim_unused(q);
        }
        if (r) {
            r->used = d_used;
            r->is_negative = n->is_negative;
            trim_unused(r);
        }
    }

    ttak_bigint_free(&n_tmp, now);
    ttak_bigint_free(&d_tmp, now);

    return ok;
}

_Bool ttak_bigint_mod(ttak_bigint_t *r, const ttak_bigint_t *n, const ttak_bigint_t *d, uint64_t now) {
    return ttak_bigint_div(NULL, r, n, d, now);
}

char* ttak_bigint_to_string(const ttak_bigint_t *bi, uint64_t now) {
    if (ttak_bigint_is_zero(bi)) {
        char *s = ttak_mem_alloc(2, __TTAK_UNSAFE_MEM_FOREVER__, now);
        if(s) strcpy(s, "0");
        return s;
    }

    // Estimate buffer size: log10(2^N) = N * log10(2) ~= N * 0.301
    size_t num_bits = ttak_bigint_get_bit_length(bi);
    size_t estimated_len = (size_t)(num_bits * 0.30103) + 2; // + sign and null
    if (bi->is_negative) estimated_len++;

    char *s = ttak_mem_alloc(estimated_len, __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!s) return NULL;

    char *p = s;
    
    ttak_bigint_t tmp, rem;
    ttak_bigint_init_copy(&tmp, bi, now);
    ttak_bigint_init(&rem, now);
    tmp.is_negative = false;

    while(!ttak_bigint_is_zero(&tmp)) {
        ttak_bigint_div_u64(&tmp, &rem, &tmp, 10, now);
        limb_t *rem_limbs = get_limbs(&rem);
        *p++ = "0123456789"[rem_limbs[0]];
    }

    if (bi->is_negative) {
        *p++ = '-';
    }
    *p = '\0';

    // Reverse the string
    char *start = s;
    char *end = p - 1;
    while(start < end) {
        char temp = *start;
        *start++ = *end;
        *end-- = temp;
    }
    
    ttak_bigint_free(&tmp, now);
    ttak_bigint_free(&rem, now);

    return s;
}

void ttak_bigint_mersenne_mod(ttak_bigint_t *bi, int p, uint64_t now) {
    (void)now;
    size_t required_limbs = ((size_t)p + 31) / 32;
    if (required_limbs > TTAK_MAX_LIMB_LIMIT) {
        return;
    }

    while (ttak_bigint_get_bit_length(bi) > (size_t)p) {
        ttak_bigint_t high, low;
        ttak_bigint_init(&high, now);
        ttak_bigint_init(&low, now);

        ensure_capacity(&low, required_limbs, now);
        
        limb_t* bi_limbs = get_limbs(bi);
        limb_t* low_limbs = get_limbs(&low);

        size_t p_limb_idx = p / 32;
        size_t p_bit_off = p % 32;

        // Low part
        memcpy(low_limbs, bi_limbs, p_limb_idx * sizeof(limb_t));
        limb_t mask = (1U << p_bit_off) - 1;
        if (p_bit_off > 0) {
            low_limbs[p_limb_idx] = bi_limbs[p_limb_idx] & mask;
        }
        low.used = required_limbs;
        trim_unused(&low);

        // High part (right shift by p)
        size_t high_limbs_needed = bi->used > p_limb_idx ? bi->used - p_limb_idx : 0;
        ensure_capacity(&high, high_limbs_needed, now);
        limb_t* high_limbs = get_limbs(&high);
        
        for(size_t i = 0; i < high_limbs_needed; ++i) {
            limb_t val = bi_limbs[p_limb_idx + i] >> p_bit_off;
            if (p_bit_off > 0 && (p_limb_idx + i + 1) < bi->used) {
                val |= bi_limbs[p_limb_idx + i + 1] << (32 - p_bit_off);
            }
            high_limbs[i] = val;
        }
        high.used = high_limbs_needed;
        trim_unused(&high);

        ttak_bigint_add(bi, &low, &high, now);

        ttak_bigint_free(&high, now);
        ttak_bigint_free(&low, now);
    }

    // Final check: if bi == 2^p - 1, it should be 0
    ttak_bigint_t mersenne_prime;
    ttak_bigint_init(&mersenne_prime, now);
    ttak_bigint_set_u64(&mersenne_prime, 1, now);
    // left shift by p
    size_t limb_shift = p / 32;
    size_t bit_shift = p % 32;
    ensure_capacity(&mersenne_prime, limb_shift + 1, now);
    get_limbs(&mersenne_prime)[limb_shift] = 1UL << bit_shift;
    mersenne_prime.used = limb_shift + 1;
    
    ttak_bigint_t one;
    ttak_bigint_init_u64(&one, 1, now);
    ttak_bigint_sub(&mersenne_prime, &mersenne_prime, &one, now);

    if (ttak_bigint_cmp(bi, &mersenne_prime) == 0) {
        ttak_bigint_set_u64(bi, 0, now);
    }

    ttak_bigint_free(&mersenne_prime, now);
    ttak_bigint_free(&one, now);
}

bool ttak_bigint_export_u64(const ttak_bigint_t *bi, uint64_t *value_out) {
    if (!bi || bi->is_negative || bi->used > 2) {
        if (value_out) *value_out = 0;
        return false;
    }
    const limb_t *limbs = get_const_limbs(bi);
    uint64_t value = 0;
    if (bi->used > 0) value |= limbs[0];
    if (bi->used > 1) value |= (uint64_t)limbs[1] << 32;
    if (value_out) *value_out = value;
    return true;
}

#include <ttak/security/sha256.h>

void ttak_bigint_to_hex_hash(const ttak_bigint_t *bi, char out[65]) {
    SHA256_CTX ctx;
    sha256_init(&ctx);
    if (bi && bi->used > 0) {
        const limb_t *limbs = get_const_limbs(bi);
        sha256_update(&ctx, (const uint8_t *)limbs, bi->used * sizeof(limb_t));
    }
    uint8_t digest[32];
    sha256_final(&ctx, digest);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        out[i * 2] = hex[(digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    out[64] = '\0';
}

void ttak_bigint_format_prefix(const ttak_bigint_t *bi, char *dest, size_t dest_cap) {
    if (!dest || dest_cap == 0) {
        if (dest) dest[0] = '\0';
        return;
    }
    
    char* s = ttak_bigint_to_string(bi, 0);
    if (!s) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, s, dest_cap - 1);
    dest[dest_cap - 1] = '\0';
    ttak_mem_free(s);
}

void ttak_bigint_hash(const ttak_bigint_t *bi, uint8_t out[32]) {
    SHA256_CTX ctx;
    sha256_init(&ctx);
    if (bi && bi->used > 0) {
        const limb_t *limbs = get_const_limbs(bi);
        sha256_update(&ctx, (const uint8_t *)limbs, bi->used * sizeof(limb_t));
    }
    sha256_final(&ctx, out);
}
