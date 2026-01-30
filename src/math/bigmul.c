#include <ttak/math/bigmul.h>

void ttak_bigmul_init(ttak_bigmul_t *bm, uint64_t now) {
    ttak_bigint_init(&bm->lhs, now);
    ttak_bigint_init(&bm->rhs, now);
    ttak_bigint_init(&bm->product, now);
}

void ttak_bigmul_free(ttak_bigmul_t *bm, uint64_t now) {
    ttak_bigint_free(&bm->lhs, now);
    ttak_bigint_free(&bm->rhs, now);
    ttak_bigint_free(&bm->product, now);
}
