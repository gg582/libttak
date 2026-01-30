#ifndef TTAK_INTERNAL_APP_TYPES_H
#define TTAK_INTERNAL_APP_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdalign.h>
#include <pthread.h>

/**
 * @brief "Fortress" Magic Number for header validation.
 */
#define TTAK_MAGIC_NUMBER 0x5454414B

/**
 * @brief Safety limit for mathematical operations to prevent OOM/Overflow.
 * 16 million limbs (approx. 64MB per bigint).
 */
#define TTAK_MAX_LIMB_LIMIT 0x1000000 

/**
 * @brief High Watermark for memory pressure backpressure (512MB).
 */
#define TTAK_MEM_HIGH_WATERMARK (512ULL * 1024ULL * 1024ULL)

/**
 * @brief Internal error codes mapping.
 */
#define ERR_TTAK_MATH_ERR   -206
#define ERR_TTAK_INV_ACC    -205

/**
 * @brief Sentinel for invalidated references.
 */
#define SAFE_NULL NULL

/**
 * @brief "Fortress" Memory Header.
 * 64-byte aligned to prevent False Sharing and ensure user pointer alignment.
 * Total size is 128 bytes to maintain 64-byte alignment for user data.
 */
typedef struct {
    alignas(64) uint32_t magic;         /**< 0x5454414B */
    uint32_t checksum;      /**< Metadata checksum */
    uint64_t created_tick;  /**< Creation timestamp */
    uint64_t expires_tick;  /**< Expiration timestamp */
    uint64_t access_count;  /**< Atomic access audit counter */
    uint64_t pin_count;     /**< Atomic reference count for pinning */
    size_t   size;          /**< User-requested size */
    pthread_mutex_t lock;   /**< Per-header synchronization */
    _Bool    freed;         /**< Allocation status */
    _Bool    is_const;      /**< Immutability hint */
    _Bool    is_volatile;   /**< Volatility hint */
    _Bool    allow_direct_access; /**< Safety bypass flag */
    _Bool    is_huge;       /**< Mapped via hugepages */
    char     reserved[35];  /**< Explicit padding for 128-byte header on x64 */
} ttak_mem_header_t;

/**
 * @brief Calculates a 32-bit checksum of the header metadata.
 */
static inline uint32_t ttak_calc_header_checksum(const ttak_mem_header_t *h) {
    uint32_t sum = h->magic;
    sum ^= (uint32_t)h->created_tick;
    sum ^= (uint32_t)(h->created_tick >> 32);
    sum ^= (uint32_t)h->expires_tick;
    sum ^= (uint32_t)(h->expires_tick >> 32);
    sum ^= (uint32_t)h->size;
#if defined(__LP64__) || defined(_WIN64)
    sum ^= (uint32_t)(h->size >> 32);
#endif
    return sum;
}

#endif // TTAK_INTERNAL_APP_TYPES_H
