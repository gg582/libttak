#ifndef TTAK_SIMD_MATH_H
#define TTAK_SIMD_MATH_H

#include <stdint.h>

#if defined(__AVX512F__)
#include <immintrin.h>
#define TTAK_HAS_SIMD 1
typedef __m512i ttak_simd_vec_t;
#elif defined(__AVX2__)
#include <immintrin.h>
#define TTAK_HAS_SIMD 1
typedef __m256i ttak_simd_vec_t;
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#define TTAK_HAS_SIMD 1
typedef uint32x4_t ttak_simd_vec_t;
#else
#define TTAK_HAS_SIMD 0
#endif

/**
 * @brief Vectorized limb addition.
 */
static inline void ttak_vec_add(uint32_t *res, const uint32_t *a, const uint32_t *b, size_t n) {
#if defined(__AVX2__)
    /* Use aligned load/store for better performance if memory is 32-byte aligned */
    for (size_t i = 0; i < n; i += 8) {
        __m256i va = _mm256_load_si256((__m256i*)&a[i]);
        __m256i vb = _mm256_load_si256((__m256i*)&b[i]);
        __m256i vr = _mm256_add_epi32(va, vb);
        _mm256_store_si256((__m256i*)&res[i], vr);
    }
#elif defined(__ARM_NEON)
    for (size_t i = 0; i < n; i += 4) {
        uint32x4_t va = vld1q_u32(&a[i]);
        uint32x4_t vb = vld1q_u32(&b[i]);
        uint32x4_t vr = vaddq_u32(va, vb);
        vst1q_u32(&res[i], vr);
    }
#else
    for (size_t i = 0; i < n; i++) {
        res[i] = a[i] + b[i];
    }
#endif
}

#endif // TTAK_SIMD_MATH_H
