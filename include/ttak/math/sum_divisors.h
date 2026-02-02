#ifndef TTAK_MATH_SUM_DIVISORS_H
#define TTAK_MATH_SUM_DIVISORS_H

#include <ttak/math/bigint.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Calculates the sum of proper divisors for a 64-bit unsigned integer.
 *
 * This function calculates sigma(n) - n. It uses 128-bit intermediate
 * arithmetic to detect overflow.
 *
 * @param n The number.
 * @param result_out A pointer to store the sum of proper divisors.
 * @return `true` if the calculation was successful and fits in a uint64_t.
 *         `false` if the sum of divisors overflows uint64_t. In this case,
 *         the value in result_out is undefined.
 */
bool ttak_sum_proper_divisors_u64(uint64_t n, uint64_t *result_out);

/**
 * @brief Calculates the sum of proper divisors for a big integer.
 *
 * @param n The bigint number.
 * @param result_out The bigint to store the sum of proper divisors.
 * @param now The current timestamp for memory allocation.
 * @return `true` on success, `false` on memory allocation failure.
 */
bool ttak_sum_proper_divisors_big(const ttak_bigint_t *n, ttak_bigint_t *result_out, uint64_t now);


#endif // TTAK_MATH_SUM_DIVISORS_H
