#include <ttak/async/promise.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>

ttak_promise_t *ttak_promise_create(uint64_t now) {
    ttak_promise_t *promise = (ttak_promise_t *)ttak_mem_alloc(sizeof(ttak_promise_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!promise) return NULL;

    promise->future = (ttak_future_t *)ttak_mem_alloc(sizeof(ttak_future_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!promise->future) {
        ttak_mem_free(promise);
        return NULL;
    }

    promise->future->ready = false;
    promise->future->result = NULL;
    pthread_mutex_init(&promise->future->mutex, NULL);
    pthread_cond_init(&promise->future->cond, NULL);

    return promise;
}

void ttak_promise_set_value(ttak_promise_t *promise, void *val, uint64_t now) {
    if (!ttak_mem_access(promise, now) || !promise->future) return;
    pthread_mutex_lock(&promise->future->mutex);
    promise->future->result = val;
    promise->future->ready = true;
    pthread_cond_broadcast(&promise->future->cond);
    pthread_mutex_unlock(&promise->future->mutex);
}

ttak_future_t *ttak_promise_get_future(ttak_promise_t *promise) {
    return promise ? promise->future : NULL;
}
