#ifndef TTAK_SYNC_H
#define TTAK_SYNC_H

#include <pthread.h>
#include <stdint.h>

typedef pthread_mutex_t ttak_mutex_t;
typedef pthread_cond_t ttak_cond_t;

/**
 * @brief Shared resource structure.
 */
typedef struct tt_type_shared {
    void            *data;
    int             size;
    pthread_mutex_t mutex;
    uint64_t        ts;    /** Inherited timestamp (tick count) */
} tt_type_shared_t;

// Mutex
int ttak_mutex_init(ttak_mutex_t *mutex);
int ttak_mutex_lock(ttak_mutex_t *mutex);
int ttak_mutex_unlock(ttak_mutex_t *mutex);
int ttak_mutex_destroy(ttak_mutex_t *mutex);

// Condition Variable
int ttak_cond_init(ttak_cond_t *cond);
int ttak_cond_wait(ttak_cond_t *cond, ttak_mutex_t *mutex);
int ttak_cond_signal(ttak_cond_t *cond);
int ttak_cond_broadcast(ttak_cond_t *cond);
int ttak_cond_destroy(ttak_cond_t *cond);

#endif // TTAK_SYNC_H