#include <stdint.h>

#if defined(__TINYC__) && defined(__attribute__)
#undef __attribute__
#endif

#if defined(__TINYC__) || defined(__GNUC__) || defined(__clang__)
#define TTAK_FLOATUNDIDF_WEAK __attribute__((weak))
#else
#define TTAK_FLOATUNDIDF_WEAK
#endif

/*
 * Some minimal C runtimes (such as tcc without libgcc/clang_rt helpers)
 * do not provide the compiler builtin __floatundidf, which is the helper
 * that converts a 64-bit unsigned integer into a double.  Our codebase
 * occasionally performs such conversions, so we provide a local fallback
 * implementation that follows the usual IEEE-754 encoding rules and
 * rounds to nearest-even.
 */

typedef union {
    uint64_t u;
    double d;
} ttak_double_shape_t;

static int ttak_leading_zeros_u64(uint64_t value) {
    int count = 0;
    while ((value & 0x8000000000000000ULL) == 0) {
        value <<= 1;
        count++;
    }
    return count;
}

TTAK_FLOATUNDIDF_WEAK double __floatundidf(uint64_t value) {
    if (value == 0) {
        return 0.0;
    }

    int lz = ttak_leading_zeros_u64(value);
    int msb_index = 63 - lz;
    int exponent = msb_index;
    uint64_t mantissa;

    if (msb_index <= 52) {
        mantissa = value << (52 - msb_index);
    } else {
        int shift = msb_index - 52;
        uint64_t rem_mask = (1ULL << shift) - 1;
        uint64_t truncated = value >> shift;
        uint64_t remainder = value & rem_mask;
        uint64_t halfway = 1ULL << (shift - 1);

        if (remainder > halfway || (remainder == halfway && (truncated & 1ULL))) {
            truncated++;
            if (truncated == (1ULL << 53)) {
                truncated >>= 1;
                exponent++;
            }
        }
        mantissa = truncated;
    }

    mantissa &= (1ULL << 52) - 1;
    uint64_t exponent_bits = (uint64_t)(exponent + 1023) << 52;

    ttak_double_shape_t shape;
    shape.u = exponent_bits | mantissa;
    return shape.d;
}

#undef TTAK_FLOATUNDIDF_WEAK
