#ifndef TTAK_MATH_BIGREAL_H
#define TTAK_MATH_BIGREAL_H

#include <ttak/math/bigint.h>

/**
 * @brief Arbitrary-precision real number engine.
 */
typedef struct ttak_bigreal {
    ttak_bigint_t mantissa;
    int64_t       exponent;
    alignas(max_align_t) char padding[0];
} ttak_bigreal_t;

typedef ttak_bigreal_t tt_big_r_t;

void ttak_bigreal_init(ttak_bigreal_t *br, uint64_t now);
void ttak_bigreal_free(ttak_bigreal_t *br, uint64_t now);

#endif // TTAK_MATH_BIGREAL_H
