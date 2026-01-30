#include <ttak/sync/sync.h>

int ttak_mutex_init(ttak_mutex_t *mutex) {
    return pthread_mutex_init(mutex, NULL);
}

int ttak_mutex_lock(ttak_mutex_t *mutex) {
    return pthread_mutex_lock(mutex);
}

int ttak_mutex_unlock(ttak_mutex_t *mutex) {
    return pthread_mutex_unlock(mutex);
}

int ttak_mutex_destroy(ttak_mutex_t *mutex) {
    return pthread_mutex_destroy(mutex);
}

int ttak_cond_init(ttak_cond_t *cond) {
    return pthread_cond_init(cond, NULL);
}

int ttak_cond_wait(ttak_cond_t *cond, ttak_mutex_t *mutex) {
    return pthread_cond_wait(cond, mutex);
}

int ttak_cond_signal(ttak_cond_t *cond) {
    return pthread_cond_signal(cond);
}

int ttak_cond_broadcast(ttak_cond_t *cond) {
    return pthread_cond_broadcast(cond);
}

int ttak_cond_destroy(ttak_cond_t *cond) {
    return pthread_cond_destroy(cond);
}
