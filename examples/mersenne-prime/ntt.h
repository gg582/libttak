#ifndef TTAK_NTT_H
#define TTAK_NTT_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Performs Number Theoretic Transform for fast bigint multiplication.
 * 
 * Uses a prime field (e.g., p = 0xffffffff00000001) to avoid floating point errors.
 */
void ttak_ntt(uint64_t *a, size_t n, bool inverse);

#endif // TTAK_NTT_H
