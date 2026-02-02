#include <ttak/math/sum_divisors.h>
#include <ttak/math/factor.h>
#include <ttak/mem/mem.h>

// Formula for sum of divisors: σ(n) = Π (p_i^(a_i+1) - 1) / (p_i - 1)

bool ttak_sum_proper_divisors_u64(uint64_t n, uint64_t *result_out) {
    if (n <= 1) {
        *result_out = 0;
        return true;
    }

    uint64_t now = 0; // No memory allocation with timeout
    ttak_prime_factor_t *factors = NULL;
    size_t count = 0;

    if (ttak_factor_u64(n, &factors, &count, now) != 0) {
        return false; // Factorization failed
    }

    unsigned __int128 sum_divs = 1;

    for (size_t i = 0; i < count; ++i) {
        uint64_t p = factors[i].p;
        uint32_t a = factors[i].a;

        unsigned __int128 term_num = 1;
        for(uint32_t j = 0; j < a + 1; ++j) {
            term_num *= p;
        }
        term_num -= 1;

        uint64_t term_den = p - 1;
        
        sum_divs = sum_divs * term_num / term_den;
    }

    ttak_mem_free(factors);

    if (sum_divs > (unsigned __int128)2 * UINT64_MAX) {
        // If sum_divs is huge, it will definitely overflow after subtracting n.
        return false;
    }
    
    uint64_t proper_sum;
    if (sum_divs < n) { // Should not happen for n > 1
        proper_sum = 0;
    } else {
        proper_sum = (uint64_t)(sum_divs - n);
    }

    if (sum_divs - n > UINT64_MAX) {
        return false; // Overflow
    }

    *result_out = proper_sum;
    return true;
}

bool ttak_sum_proper_divisors_big(const ttak_bigint_t *n, ttak_bigint_t *result_out, uint64_t now) {
    if (ttak_bigint_is_zero(n) || ttak_bigint_cmp_u64(n, 1) <= 0) {
        ttak_bigint_set_u64(result_out, 0, now);
        return true;
    }

    ttak_prime_factor_big_t *factors = NULL;
    size_t count = 0;

    if (ttak_factor_big(n, &factors, &count, now) != 0) {
        return false; // Factorization failed
    }

    ttak_bigint_t sum_divs, term_num, term_den, temp;
    ttak_bigint_init_u64(&sum_divs, 1, now);
    ttak_bigint_init(&term_num, now);
    ttak_bigint_init(&term_den, now);
    ttak_bigint_init(&temp, now);

    bool ok = true;

    for (size_t i = 0; i < count; ++i) {
        ttak_bigint_t *p = &factors[i].p;
        uint32_t a = factors[i].a;

        // Calculate p^(a+1)
        ttak_bigint_set_u64(&term_num, 1, now);
        for (uint32_t j = 0; j < a + 1; ++j) {
            if (!ttak_bigint_mul(&term_num, &term_num, p, now)) { ok = false; break; }
        }
        if (!ok) break;

        // term_num = p^(a+1) - 1
        ttak_bigint_t one;
        ttak_bigint_init_u64(&one, 1, now);
        if (!ttak_bigint_sub(&term_num, &term_num, &one, now)) { ok = false; ttak_bigint_free(&one, now); break; }

        // term_den = p - 1
        if (!ttak_bigint_sub(&term_den, p, &one, now)) { ok = false; ttak_bigint_free(&one, now); break; }
        ttak_bigint_free(&one, now);

        // temp = term_num / term_den
        if (!ttak_bigint_div(&temp, NULL, &term_num, &term_den, now)) { ok = false; break; }

        // sum_divs = sum_divs * temp
        if (!ttak_bigint_mul(&sum_divs, &sum_divs, &temp, now)) { ok = false; break; }
    }

    // Free factors
    for(size_t i = 0; i < count; ++i) {
        ttak_bigint_free(&factors[i].p, now);
    }
    ttak_mem_free(factors);

    ttak_bigint_free(&term_num, now);
    ttak_bigint_free(&term_den, now);
    ttak_bigint_free(&temp, now);

    if (!ok) {
        ttak_bigint_free(&sum_divs, now);
        return false;
    }

    // result = sum_divs - n
    if (!ttak_bigint_sub(result_out, &sum_divs, n, now)) {
        ttak_bigint_free(&sum_divs, now);
        return false;
    }
    
    ttak_bigint_free(&sum_divs, now);
    return true;
}
