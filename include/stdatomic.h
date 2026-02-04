#if defined(__has_include_next) && !defined(__TINYC__)
#  if __has_include_next(<stdatomic.h>)
#    include_next <stdatomic.h>
#    define __TTAK_STDATOMIC_SYSTEM_INCLUDED
#  endif
#endif

#ifndef TTAK_PORTABLE_STDATOMIC_H
#define TTAK_PORTABLE_STDATOMIC_H

#if defined(__TINYC__) || !defined(__TTAK_STDATOMIC_SYSTEM_INCLUDED)
#define __TTAK_NEEDS_PORTABLE_STDATOMIC__ 1
#else
#define __TTAK_NEEDS_PORTABLE_STDATOMIC__ 0
#endif

#if __TTAK_NEEDS_PORTABLE_STDATOMIC__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#ifndef _Atomic
#define _Atomic volatile
#endif

typedef enum {
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
} memory_order;

typedef volatile bool atomic_bool;
typedef volatile size_t atomic_size_t;
typedef volatile uint_fast64_t atomic_uint_fast64_t;

typedef struct {
    volatile unsigned char _value;
} atomic_flag;

extern pthread_mutex_t __ttak_atomic_global_lock;

#define ATOMIC_FLAG_INIT {0}

#define __TT_ATOMIC_LOCK() pthread_mutex_lock(&__ttak_atomic_global_lock)
#define __TT_ATOMIC_UNLOCK() pthread_mutex_unlock(&__ttak_atomic_global_lock)

#define atomic_thread_fence(order) do { (void)(order); __TT_ATOMIC_LOCK(); __TT_ATOMIC_UNLOCK(); } while (0)

#define atomic_init(obj, value) \
    do { atomic_store_explicit((obj), (value), memory_order_seq_cst); } while (0)

#define atomic_store_explicit(obj, desired, order) \
    do { (void)(order); __TT_ATOMIC_LOCK(); *(obj) = (desired); __TT_ATOMIC_UNLOCK(); } while (0)

#define atomic_store(obj, desired) \
    atomic_store_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_load_explicit(obj, order) \
    ({ __TT_ATOMIC_LOCK(); __typeof__(*(obj)) __val = *(obj); (void)(order); __TT_ATOMIC_UNLOCK(); __val; })

#define atomic_load(obj) \
    atomic_load_explicit((obj) , memory_order_seq_cst)

#define atomic_fetch_add_explicit(obj, operand, order) \
    ({ __TT_ATOMIC_LOCK(); __typeof__(*(obj)) __old = *(obj); *(obj) = __old + (operand); (void)(order); __TT_ATOMIC_UNLOCK(); __old; })

#define atomic_fetch_add(obj, operand) \
    atomic_fetch_add_explicit((obj), (operand), memory_order_seq_cst)

#define atomic_fetch_sub_explicit(obj, operand, order) \
    ({ __TT_ATOMIC_LOCK(); __typeof__(*(obj)) __old = *(obj); *(obj) = __old - (operand); (void)(order); __TT_ATOMIC_UNLOCK(); __old; })

#define atomic_fetch_sub(obj, operand) \
    atomic_fetch_sub_explicit((obj), (operand), memory_order_seq_cst)

#define atomic_compare_exchange_weak_explicit(obj, expected, desired, success, failure) \
    ({ bool __ok; (void)(success); (void)(failure); __TT_ATOMIC_LOCK(); \
       if (*(obj) == *(expected)) { *(obj) = (desired); __ok = true; } \
       else { *(expected) = *(obj); __ok = false; } \
       __TT_ATOMIC_UNLOCK(); __ok; })

#define atomic_compare_exchange_weak(obj, expected, desired) \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired), memory_order_seq_cst, memory_order_seq_cst)

static inline bool atomic_flag_test_and_set_explicit(volatile atomic_flag *obj, memory_order order) {
    (void)order;
    __TT_ATOMIC_LOCK();
    bool prev = obj->_value != 0;
    obj->_value = 1;
    __TT_ATOMIC_UNLOCK();
    return prev;
}

static inline void atomic_flag_clear_explicit(volatile atomic_flag *obj, memory_order order) {
    (void)order;
    __TT_ATOMIC_LOCK();
    obj->_value = 0;
    __TT_ATOMIC_UNLOCK();
}

#define atomic_flag_test_and_set(obj) \
    atomic_flag_test_and_set_explicit((obj), memory_order_seq_cst)

#define atomic_flag_clear(obj) \
    atomic_flag_clear_explicit((obj), memory_order_seq_cst)

#endif /* __TTAK_NEEDS_PORTABLE_STDATOMIC__ */

#undef __TTAK_NEEDS_PORTABLE_STDATOMIC__

#endif /* TTAK_PORTABLE_STDATOMIC_H */