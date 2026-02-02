#ifndef TTAK_MEM_H
#define TTAK_MEM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Alignment for cache-line optimization (64-byte).
 */
#define TTAK_CACHE_LINE_SIZE 64

/**
 * @brief Macro to indicate that the allocated memory should persist forever.
 */
#define __TTAK_UNSAFE_MEM_FOREVER__ ((uint64_t)-1)

/**
 * @brief Memory allocation flags.
 */
typedef enum {
    TTAK_MEM_DEFAULT = 0,
    TTAK_MEM_HUGE_PAGES = (1 << 0), /** Try to use 2MB/1GB pages */
    TTAK_MEM_CACHE_ALIGNED = (1 << 1), /** Force 64-byte alignment */
    TTAK_MEM_STRICT_CHECK = (1 << 2) /** Enable strict memory boundary checks */
} ttak_mem_flags_t;

/**
 * @brief Unified memory allocation with lifecycle management and hardware optimization.
 */
void *ttak_mem_alloc_safe(size_t size, uint64_t lifetime_ticks, uint64_t now, _Bool is_const, _Bool is_volatile, _Bool allow_direct, _Bool is_root, ttak_mem_flags_t flags);

/**
 * @brief Reallocates memory with lifecycle management.
 */
void *ttak_mem_realloc_safe(void *ptr, size_t new_size, uint64_t lifetime_ticks, uint64_t now, _Bool is_root, ttak_mem_flags_t flags);

/**
 * @brief Accesses the memory block, verifying its lifecycle and security.
 */
void *ttak_mem_access(void *ptr, uint64_t now);

/**
 * @brief Frees the memory block and removes it from the global shadow map.
 */
void ttak_mem_free(void *ptr);

/**
 * @brief Inspects and returns pointers that are expired or have abnormal access counts.
 */
void **tt_inspect_dirty_pointers(uint64_t now, size_t *count_out);

/**
 * @brief Automatically cleans up expired memory blocks with adaptive scheduling.
 */
void tt_autoclean_dirty_pointers(uint64_t now);

/**
 * @brief Cleans up and returns abnormal pointers.
 */
void **tt_autoclean_and_inspect(uint64_t now, size_t *count_out);

/* Compatibility macros */
typedef void ttak_lifecycle_obj_t;
#define ttak_mem_alloc(size, lifetime, now) ttak_mem_alloc_safe(size, lifetime, now, false, false, true, true, TTAK_MEM_DEFAULT)
#define ttak_mem_alloc_with_flags(size, lifetime, now, flags) ttak_mem_alloc_safe(size, lifetime, now, false, false, true, true, flags)
#define ttak_mem_realloc(ptr, size, lifetime, now) ttak_mem_realloc_safe(ptr, size, lifetime, now, true, TTAK_MEM_DEFAULT)
#define ttak_mem_realloc_with_flags(ptr, size, lifetime, now, flags) ttak_mem_realloc_safe(ptr, size, lifetime, now, true, flags)

#endif // TTAK_MEM_H
