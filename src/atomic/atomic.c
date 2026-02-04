#include <ttak/atomic/atomic.h>
#include <ttak/ht/map.h>
#include <ttak/mem/mem.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

pthread_mutex_t __ttak_atomic_global_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Atomic operations for uint64_t using GCC builtins.
 */
uint64_t ttak_atomic_read64(volatile uint64_t *ptr) {
    return atomic_load_explicit((_Atomic uint64_t *)ptr, memory_order_seq_cst);
}

/**
 * @brief Atomically store a 64-bit value.
 *
 * @param ptr Pointer to the atomic variable.
 * @param val Value to store.
 */
void ttak_atomic_write64(volatile uint64_t *ptr, uint64_t val) {
    atomic_store_explicit((_Atomic uint64_t *)ptr, val, memory_order_seq_cst);
}

/**
 * @brief Atomically increment a 64-bit integer.
 *
 * @param ptr Pointer to the atomic counter.
 * @return Updated value after increment.
 */
uint64_t ttak_atomic_inc64(volatile uint64_t *ptr) {
    return atomic_fetch_add_explicit((_Atomic uint64_t *)ptr, 1, memory_order_seq_cst) + 1;
}

uint64_t ttak_atomic_add64(volatile uint64_t *ptr, uint64_t delta) {
    return atomic_fetch_add_explicit((_Atomic uint64_t *)ptr, delta, memory_order_seq_cst) + delta;
}

uint64_t ttak_atomic_sub64(volatile uint64_t *ptr, uint64_t delta) {
    return atomic_fetch_sub_explicit((_Atomic uint64_t *)ptr, delta, memory_order_seq_cst) - delta;
}

/**
 * @brief Check tt_func_wrapper has expired.
 */
_Bool tt_func_has_expired(ttak_func_wrapper_t *wr, uint64_t now) {
    if (!wr) return 0;
    // Simple expiration logic: if current tick is greater than timestamp + 60000 (example 60s if tick is ms)
    // For now, let's keep it simple.
    if (now > wr->ts + 60000) { 
        wr->expired = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief Initialize a function wrapper with default bookkeeping.
 *
 * @param wr  Wrapper to initialize.
 * @param now Current timestamp used for metadata.
 */
void func_wrapper_init(ttak_func_wrapper_t *wr, uint64_t now) {
    if (!wr) return;
    
    memset(wr, 0, sizeof(ttak_func_wrapper_t));
    
    wr->type = __TT_FUNC_ATOMIC__;
    pthread_mutex_init(&wr->mutex, NULL);
    wr->ts = now;
    wr->func_has_expired = tt_func_has_expired;
    wr->expired = 0;
    
    // Initialize map with default capacity, e.g., 16
    tt_map_t *map = ttak_create_map(16, now);
    if (map) {
        wr->tbl = *map;
        ttak_mem_free(map); // Shallow copy struct, free the container since tbl is a struct, not a pointer in the wrapper
    }
}

/**
 * @brief Execute the wrapped function under the wrapper mutex.
 *
 * @param f   Function wrapper to execute.
 * @param now Current timestamp for expiration checks.
 * @return Stored return value pointer, or NULL if execution is skipped.
 */
void *atomic_function_execute(tt_func_wr_t *f, uint64_t now) {
    if (!f) return NULL;

    pthread_mutex_lock(&f->mutex);
    
    if (f->func_has_expired && f->func_has_expired(f, now)) {
        f->expired = 1; // Ensure flag is set
        pthread_mutex_unlock(&f->mutex);
        return NULL;
    }
    
    if (f->fun) {
        f->fun();
    }
    
    pthread_mutex_unlock(&f->mutex);
    return f->ret; // Return the stored return value
}
