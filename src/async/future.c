#include <ttak/async/future.h>
#include <stddef.h>

void *ttak_future_get(ttak_future_t *future) {
    if (!future) return NULL;
    pthread_mutex_lock(&future->mutex);
    while (!future->ready) {
        pthread_cond_wait(&future->cond, &future->mutex);
    }
    void *res = future->result;
    pthread_mutex_unlock(&future->mutex);
    return res;
}
