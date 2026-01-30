#ifndef TTAK_ASYNC_PROMISE_H
#define TTAK_ASYNC_PROMISE_H

#include <stdint.h>
#include <ttak/async/future.h>

typedef struct ttak_promise {
    ttak_future_t *future;
} ttak_promise_t;

ttak_promise_t *ttak_promise_create(uint64_t now);
void ttak_promise_set_value(ttak_promise_t *promise, void *val, uint64_t now);
ttak_future_t *ttak_promise_get_future(ttak_promise_t *promise);

#endif // TTAK_ASYNC_PROMISE_H
