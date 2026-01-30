#include <ttak/math/bigcomplex.h>
#include <ttak/mem/mem.h>

void ttak_bigcomplex_init(ttak_bigcomplex_t *bc, uint64_t now) {
    ttak_bigreal_init(&bc->real, now);
    ttak_bigreal_init(&bc->imag, now);
}

void ttak_bigcomplex_free(ttak_bigcomplex_t *bc, uint64_t now) {
    ttak_bigreal_free(&bc->real, now);
    ttak_bigreal_free(&bc->imag, now);
}

_Bool ttak_bigcomplex_add(ttak_bigcomplex_t *dst, const ttak_bigcomplex_t *lhs, const ttak_bigcomplex_t *rhs, uint64_t now) {
    if (!ttak_bigreal_add(&dst->real, &lhs->real, &rhs->real, now)) {
        return false;
    }
    if (!ttak_bigreal_add(&dst->imag, &lhs->imag, &rhs->imag, now)) {
        return false;
    }
    return true;
}
