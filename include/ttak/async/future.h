#ifndef TTAK_ASYNC_FUTURE_H
#define TTAK_ASYNC_FUTURE_H

#include <pthread.h>
#include <stdbool.h>

typedef struct ttak_future {
    void            *result;
    _Bool           ready;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
} ttak_future_t;

void *ttak_future_get(ttak_future_t *future);

#endif // TTAK_ASYNC_FUTURE_H
