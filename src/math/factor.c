#include <ttak/math/factor.h>
#include <ttak/mem/mem.h>
#include <string.h>
#include <math.h>

// Internal helper to add a factor to a dynamic array.
static int add_factor(uint64_t p, ttak_prime_factor_t **factors, size_t *count, size_t *capacity, uint64_t now) {
    for (size_t i = 0; i < *count; ++i) {
        if ((*factors)[i].p == p) {
            (*factors)[i].a++;
            return 0;
        }
    }

    if (*count == *capacity) {
        size_t new_capacity = (*capacity == 0) ? 8 : *capacity * 2;
        ttak_prime_factor_t *new_factors = ttak_mem_realloc(*factors, new_capacity * sizeof(ttak_prime_factor_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (!new_factors) return -1; // Allocation failure
        *factors = new_factors;
        *capacity = new_capacity;
    }

    (*factors)[*count].p = p;
    (*factors)[*count].a = 1;
    (*count)++;
    return 0;
}

int ttak_factor_u64(uint64_t n, ttak_prime_factor_t **factors_out, size_t *count_out, uint64_t now) {
    if (n <= 1) {
        *factors_out = NULL;
        *count_out = 0;
        return 0;
    }

    ttak_prime_factor_t *factors = NULL;
    size_t count = 0;
    size_t capacity = 0;
    uint64_t temp_n = n;

    while (temp_n % 2 == 0) {
        if (add_factor(2, &factors, &count, &capacity, now) != 0) goto fail;
        temp_n /= 2;
    }

    for (uint64_t i = 3; i <= sqrt(temp_n); i += 2) {
        while (temp_n % i == 0) {
            if (add_factor(i, &factors, &count, &capacity, now) != 0) goto fail;
            temp_n /= i;
        }
    }

    if (temp_n > 2) {
        if (add_factor(temp_n, &factors, &count, &capacity, now) != 0) goto fail;
    }

    *factors_out = factors;
    *count_out = count;
    return 0;

fail:
    ttak_mem_free(factors);
    *factors_out = NULL;
    *count_out = 0;
    return -1;
}

// Internal helper for big integer factorization
static int add_factor_big(const ttak_bigint_t *p, ttak_prime_factor_big_t **factors, size_t *count, size_t *capacity, uint64_t now) {
    for (size_t i = 0; i < *count; ++i) {
        if (ttak_bigint_cmp(&(*factors)[i].p, p) == 0) {
            (*factors)[i].a++;
            return 0;
        }
    }

    if (*count == *capacity) {
        size_t new_capacity = (*capacity == 0) ? 8 : *capacity * 2;
        ttak_prime_factor_big_t *new_factors = ttak_mem_realloc(*factors, new_capacity * sizeof(ttak_prime_factor_big_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (!new_factors) return -1;
        *factors = new_factors;
        *capacity = new_capacity;
    }

    ttak_bigint_init_copy(&(*factors)[*count].p, p, now);
    (*factors)[*count].a = 1;
    (*count)++;
    return 0;
}


// NOTE: This is a trial division implementation for big integers. It is correct but will be
// very slow for numbers with large prime factors. For a "fast" implementation as requested,
// this should be replaced or augmented with algorithms like Pollard's Rho or the
// Elliptic Curve Method (ECM), especially for factors beyond a certain bit size.
int ttak_factor_big(const ttak_bigint_t *n, ttak_prime_factor_big_t **factors_out, size_t *count_out, uint64_t now) {
    if (ttak_bigint_is_zero(n) || ttak_bigint_cmp_u64(n, 1) <= 0) {
        *factors_out = NULL;
        *count_out = 0;
        return 0;
    }

    ttak_prime_factor_big_t *factors = NULL;
    size_t count = 0;
    size_t capacity = 0;

    ttak_bigint_t temp_n, rem, p;
    ttak_bigint_init_copy(&temp_n, n, now);
    ttak_bigint_init(&rem, now);
    ttak_bigint_init_u64(&p, 2, now);

    // Handle factor 2
    ttak_bigint_mod_u64(&rem, &temp_n, 2, now);
    while (ttak_bigint_is_zero(&rem)) {
        if (add_factor_big(&p, &factors, &count, &capacity, now) != 0) goto big_fail;
        ttak_bigint_div_u64(&temp_n, NULL, &temp_n, 2, now);
        ttak_bigint_mod_u64(&rem, &temp_n, 2, now);
    }

    // Handle odd factors
    ttak_bigint_set_u64(&p, 3, now);
    ttak_bigint_t p_squared;
    ttak_bigint_init(&p_squared, now);
    ttak_bigint_mul(&p_squared, &p, &p, now);

    while (ttak_bigint_cmp(&p_squared, &temp_n) <= 0) {
        ttak_bigint_mod(&rem, &temp_n, &p, now);
        while (ttak_bigint_is_zero(&rem)) {
            if (add_factor_big(&p, &factors, &count, &capacity, now) != 0) goto big_fail;
            ttak_bigint_div(&temp_n, NULL, &temp_n, &p, now);
            ttak_bigint_mod(&rem, &temp_n, &p, now);
        }
        ttak_bigint_add_u64(&p, &p, 2, now);
        ttak_bigint_mul(&p_squared, &p, &p, now);
    }

    if (ttak_bigint_cmp_u64(&temp_n, 1) > 0) {
        if (add_factor_big(&temp_n, &factors, &count, &capacity, now) != 0) goto big_fail;
    }

    *factors_out = factors;
    *count_out = count;

    ttak_bigint_free(&temp_n, now);
    ttak_bigint_free(&rem, now);
    ttak_bigint_free(&p, now);
    ttak_bigint_free(&p_squared, now);
    return 0;

big_fail:
    for(size_t i = 0; i < count; ++i) {
        ttak_bigint_free(&factors[i].p, now);
    }
    ttak_mem_free(factors);
    *factors_out = NULL;
    *count_out = 0;
    ttak_bigint_free(&temp_n, now);
    ttak_bigint_free(&rem, now);
    ttak_bigint_free(&p, now);
    ttak_bigint_free(&p_squared, now);
    return -1;
}
