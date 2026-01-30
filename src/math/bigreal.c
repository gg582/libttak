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
