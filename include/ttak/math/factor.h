#ifndef TTAK_MATH_FACTOR_H
#define TTAK_MATH_FACTOR_H

#include <ttak/math/bigint.h>
#include <stdint.h>
#include <stddef.h>

// Structure to hold a prime factor and its exponent.
typedef struct {
    uint64_t p; // The prime factor
    uint32_t a; // The exponent
} ttak_prime_factor_t;

// Structure to hold prime factors for big integers.
typedef struct {
    ttak_bigint_t p; // The prime factor
    uint32_t a;      // The exponent
} ttak_prime_factor_big_t;


/**
 * @brief Factorizes a 64-bit unsigned integer.
 * @param n The number to factor.
 * @param factors_out A pointer to an array of factors, which will be allocated by the function.
 * @param count_out A pointer to store the number of prime factors found.
 * @param now The current timestamp for memory allocation.
 * @return 0 on success, non-zero on failure. The caller is responsible for freeing factors_out.
 */
int ttak_factor_u64(uint64_t n, ttak_prime_factor_t **factors_out, size_t *count_out, uint64_t now);

/**
 * @brief Factorizes a big integer.
 * @param n The bigint to factor.
 * @param factors_out A pointer to an array of big integer factors, allocated by the function.
 * @param count_out A pointer to store the number of prime factors found.
 * @param now The current timestamp for memory allocation.
 * @return 0 on success, non-zero on failure. The caller must free the allocated factors array
 *         and the bigints within it.
 */
int ttak_factor_big(const ttak_bigint_t *n, ttak_prime_factor_big_t **factors_out, size_t *count_out, uint64_t now);

#endif // TTAK_MATH_FACTOR_H
