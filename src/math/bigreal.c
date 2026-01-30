#include <ttak/math/bigreal.h>
#include <ttak/mem/mem.h>
#include <string.h>

void ttak_bigreal_init(ttak_bigreal_t *br, uint64_t now) {
    ttak_bigint_init(&br->mantissa, now);
    br->exponent = 0;
}

void ttak_bigreal_free(ttak_bigreal_t *br, uint64_t now) {
    ttak_bigint_free(&br->mantissa, now);
}

_Bool ttak_bigreal_add(ttak_bigreal_t *dst, const ttak_bigreal_t *lhs, const ttak_bigreal_t *rhs, uint64_t now) {
    if (lhs->exponent != rhs->exponent) {
        return false;
    }
    dst->exponent = lhs->exponent;
    return ttak_bigint_add(&dst->mantissa, &lhs->mantissa, &rhs->mantissa, now);
}
