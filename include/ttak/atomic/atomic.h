#ifndef __TTAK_ATOMIC_H__
#define __TTAK_ATOMIC_H__

#include <ttak/ht/hash.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/**
 * @brief Generic function pointer type.
 */
typedef void (*ttak_generic_func_t)(void);

/**
 * @brief Atomic operations for uint64_t.
 */
uint64_t ttak_atomic_read64(uint64_t *ptr);
void ttak_atomic_write64(uint64_t *ptr, uint64_t val);
uint64_t ttak_atomic_inc64(uint64_t *ptr);

/**
 * @brief Wrapper structure for atomic function execution.
 */
#define __TT_FUNC_ATOMIC__ 1
#define __TT_FUNC_ASYNC__  2
#define __TT_FUNC_THREADED__ 3

typedef struct ttak_func_wrapper {
    int                 type   ; /** ATOMIC/ASYNC/THREADED            */
    pthread_mutex_t     mutex  ; /**< Mutex to ensure atomicity.      */
    uint64_t            ts     ; /**< Tick timestamp to check memory life. */
    _Bool               (*func_has_expired)(struct ttak_func_wrapper *, uint64_t); /** < Function to check if it is expired. */
    _Bool               expired;
    void                *args; /**< Pointer to arguments. */
    tt_map_t            tbl  ; /**< Table containing <order, size>. */
    ttak_generic_func_t fun  ; /**< Function to execute atomically. */
    void                *ret;  /**< Storage for return value. */
} ttak_func_wrapper_t;

/**
 * @brief Check tt_func_wrapper has expired.
 */
_Bool tt_func_has_expired(ttak_func_wrapper_t *wr, uint64_t now);
/**
 * @brief initialize tt_func_wrapper.
 */
void func_wrapper_init(ttak_func_wrapper_t *wr, uint64_t now);

/**
 * @brief Shortened alias for ttak_func_wrapper_t.
 */
typedef ttak_func_wrapper_t tt_func_wr_t;

/**
 * @brief Executes a function atomically using the provided wrapper.
 *
 * @param f Pointer to the function wrapper.
 * @param now Current tick count.
 * @return Pointer to the return value.
 */
void *atomic_function_execute(tt_func_wr_t *f, uint64_t now);

#endif // __TTAK_ATOMIC_H__
