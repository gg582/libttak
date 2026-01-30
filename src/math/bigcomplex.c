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
