#include <ttak/mem/mem.h>
#include <ttak/atomic/atomic.h>
#include <ttak/ht/map.h>
#include <ttak/timing/timing.h>
#include <ttak/mem_tree/mem_tree.h> // Include for mem tree integration
#include "../../internal/app_types.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdalign.h>
#include <stdio.h>
#include <fcntl.h>

#define TTAK_CANARY_START_MAGIC 0xDEADBEEFDEADBEEFULL
#define TTAK_CANARY_END_MAGIC   0xBEEFDEADBEEFDEADULL

static uint64_t global_mem_usage = 0;
static pthread_mutex_t global_map_lock = PTHREAD_MUTEX_INITIALIZER;
static ttak_mem_tree_t global_mem_tree; // Global instance of the mem tree

/**
 * @brief Internal validation macro.
 */
#define V_HEADER(ptr) do { \
    if (!ptr) break; \
    ttak_mem_header_t *_h = (ttak_mem_header_t *)(ptr) - 1; \
    if (_h->magic != TTAK_MAGIC_NUMBER || _h->checksum != ttak_calc_header_checksum(_h)) { \
        fprintf(stderr, "[FATAL] TTAK Memory Corruption detected at %p (Header corrupted)\n", (void*)ptr); \
        abort(); \
    } \
    if (_h->strict_check) { \
        uint64_t *canary_start_ptr = (uint64_t *)((char *)_h + sizeof(ttak_mem_header_t)); \
        uint64_t *canary_end_ptr = (uint64_t *)((char *)_h + sizeof(ttak_mem_header_t) + _h->size); \
        if (*canary_start_ptr != TTAK_CANARY_START_MAGIC) { \
            fprintf(stderr, "[FATAL] TTAK Memory Corruption detected at %p (Start canary corrupted)\n", (void*)ptr); \
            abort(); \
        } \
        if (*canary_end_ptr != TTAK_CANARY_END_MAGIC) { \
            fprintf(stderr, "[FATAL] TTAK Memory Corruption detected at %p (End canary corrupted)\n", (void*)ptr); \
            abort(); \
        } \
    } \
} while(0)

#define GET_HEADER(ptr) ((ttak_mem_header_t *)(ptr) - 1)
#define GET_USER_PTR(header) ((void *)((ttak_mem_header_t *)(header) + 1))

static tt_map_t *global_ptr_map = NULL;
static pthread_mutex_t global_init_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local _Bool in_mem_op = false;
static _Thread_local _Bool in_mem_init = false;
static _Bool global_init_done = false;

static void ensure_global_map(uint64_t now) {
    if (global_init_done || in_mem_init) return;

    pthread_mutex_lock(&global_init_lock);
    if (!global_ptr_map && !global_init_done) {
        in_mem_init = true;
        global_ptr_map = ttak_create_map(8192, now);
        ttak_mem_tree_init(&global_mem_tree); // Initialize the global mem tree
        global_init_done = true;
        in_mem_init = false;
    }
    pthread_mutex_unlock(&global_init_lock);
}

void TTAK_HOT_PATH *ttak_mem_alloc_safe(size_t size, uint64_t lifetime_ticks, uint64_t now, _Bool is_const, _Bool is_volatile, _Bool allow_direct, _Bool is_root, ttak_mem_flags_t flags) {
    size_t header_size = sizeof(ttak_mem_header_t);
    _Bool strict_check_enabled = (flags & TTAK_MEM_STRICT_CHECK);
    size_t canary_padding = strict_check_enabled ? (2 * sizeof(uint64_t)) : 0;
    size_t total_alloc_size = header_size + canary_padding + size;
    ttak_mem_header_t *header = NULL;
    _Bool is_huge = false;

    if (flags & TTAK_MEM_HUGE_PAGES) {
        header = mmap(NULL, total_alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        if (header != MAP_FAILED) {
            is_huge = true;
        } else {
            header = NULL;
        }
    }

    if (!header) {
        // Default to 64-byte alignment anyway to satisfy SIMD and prevent false sharing
        if (posix_memalign((void **)&header, 64, total_alloc_size) != 0) {
            header = NULL;
        }
    }

    if (!header && errno == ENOMEM) {
        static _Thread_local _Bool retrying = false;
        if (!retrying) {
            retrying = true;
            tt_autoclean_dirty_pointers(now);
            void *res = ttak_mem_alloc_safe(size, lifetime_ticks, now, is_const, is_volatile, allow_direct, is_root, flags);
            retrying = false;
            return res;
        }
    }

    if (!header) return NULL;

    header->magic = TTAK_MAGIC_NUMBER;
    header->created_tick = now;
    header->expires_tick = (lifetime_ticks == __TTAK_UNSAFE_MEM_FOREVER__) ? (uint64_t)-1 : now + lifetime_ticks;
    header->access_count = 0;
    header->pin_count = 0;
    header->size = size;
    header->freed = false;
    header->is_const = is_const;
    header->is_volatile = is_volatile;
    header->allow_direct_access = allow_direct;
    header->is_huge = is_huge;
    header->should_join = false; // Default to false, can be set later if needed
    header->strict_check = strict_check_enabled;
    header->canary_start = strict_check_enabled ? TTAK_CANARY_START_MAGIC : 0;
    header->canary_end = strict_check_enabled ? TTAK_CANARY_END_MAGIC : 0;
    pthread_mutex_init(&header->lock, NULL);
    header->checksum = ttak_calc_header_checksum(header);

    __atomic_add_fetch(&global_mem_usage, total_alloc_size, __ATOMIC_SEQ_CST);

    void *user_ptr = (char *)header + header_size;
    if (strict_check_enabled) {
        *((uint64_t *)user_ptr) = TTAK_CANARY_START_MAGIC;
        user_ptr = (char *)user_ptr + sizeof(uint64_t);
    }
    
    memset(user_ptr, 0, size);

    if (strict_check_enabled) {
        *((uint64_t *)((char *)user_ptr + size)) = TTAK_CANARY_END_MAGIC;
    }

    ensure_global_map(now);
    if (global_init_done && !in_mem_init && !in_mem_op) {
        pthread_mutex_lock(&global_map_lock);
        in_mem_op = true;
        ttak_insert_to_map(global_ptr_map, (uintptr_t)user_ptr, (size_t)header, now);
        ttak_mem_tree_add(&global_mem_tree, user_ptr, size, header->expires_tick, is_root); // Add to mem tree
        in_mem_op = false;
        pthread_mutex_unlock(&global_map_lock);
    }

    return user_ptr;
}

void TTAK_HOT_PATH *ttak_mem_realloc_safe(void *ptr, size_t new_size, uint64_t lifetime_ticks, uint64_t now, _Bool is_root, ttak_mem_flags_t flags) {
    V_HEADER(ptr);
    if (!ptr) {
        return ttak_mem_alloc_safe(new_size, lifetime_ticks, now, false, false, true, is_root, flags);
    }

    ttak_mem_header_t *old_header = GET_HEADER(ptr);
    pthread_mutex_lock(&old_header->lock);
    _Bool is_const = old_header->is_const;
    _Bool is_volatile = old_header->is_volatile;
    _Bool allow_direct = old_header->allow_direct_access;
    size_t old_size = old_header->size;
    _Bool old_strict_check = old_header->strict_check; // Capture old strict_check
    pthread_mutex_unlock(&old_header->lock);

    // Pass the strict_check flag to the new allocation
    ttak_mem_flags_t new_flags = flags;
    if (old_strict_check) {
        new_flags |= TTAK_MEM_STRICT_CHECK;
    } else {
        new_flags &= ~TTAK_MEM_STRICT_CHECK;
    }

    void *new_ptr = ttak_mem_alloc_safe(new_size, lifetime_ticks, now, is_const, is_volatile, allow_direct, is_root, new_flags);
    if (!new_ptr) return NULL;

    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    ttak_mem_free(ptr);
    return new_ptr;
}

void TTAK_HOT_PATH ttak_mem_free(void *ptr) {
    if (!ptr) return;
    V_HEADER(ptr); // This will check canaries if strict_check is enabled
    ttak_mem_header_t *header = GET_HEADER(ptr);

    if (!in_mem_op) {
        pthread_mutex_lock(&global_map_lock);
        in_mem_op = true;
        if (global_ptr_map) {
            ttak_delete_from_map(global_ptr_map, (uintptr_t)ptr, 0);
        }
        // Remove from heap tree
        ttak_mem_node_t *node_to_remove = ttak_mem_tree_find_node(&global_mem_tree, ptr);
        if (node_to_remove) {
            ttak_mem_tree_remove(&global_mem_tree, node_to_remove);
        }
        in_mem_op = false;
        pthread_mutex_unlock(&global_map_lock);
    }

    pthread_mutex_lock(&header->lock);
    if (header->freed) {
        pthread_mutex_unlock(&header->lock);
        return;
    }
    header->freed = true;
    
    size_t canary_padding = header->strict_check ? (2 * sizeof(uint64_t)) : 0;
    size_t total_alloc_size = sizeof(ttak_mem_header_t) + canary_padding + header->size;

    // Check pin count for delayed free
    if (__atomic_load_n(&header->pin_count, __ATOMIC_SEQ_CST) > 0) {
        // In a real implementation, we would mark it for deferred cleanup.
        // For now, we'll proceed but this is where the Pinning mechanism would act.
    }
    pthread_mutex_unlock(&header->lock);

    __atomic_sub_fetch(&global_mem_usage, total_alloc_size, __ATOMIC_SEQ_CST);
    pthread_mutex_destroy(&header->lock);

    if (header->is_huge) {
        munmap(header, total_alloc_size);
    } else {
        free(header);
    }
}

void TTAK_HOT_PATH *ttak_mem_access(void *ptr, uint64_t now) {
    if (!ptr) return SAFE_NULL;
    V_HEADER(ptr);
    ttak_mem_header_t *header = GET_HEADER(ptr);

    pthread_mutex_lock(&header->lock);
    if (header->freed || (header->expires_tick != (uint64_t)-1 && now > header->expires_tick)) {
        pthread_mutex_unlock(&header->lock);
        return SAFE_NULL;
    }
    if (!header->allow_direct_access) {
        pthread_mutex_unlock(&header->lock);
        return SAFE_NULL;
    }

    // Safe access auditing and pinning inside the lock
    ttak_atomic_inc64(&header->access_count);
    ttak_atomic_inc64(&header->pin_count);

    pthread_mutex_unlock(&header->lock);

    return ptr;
}

void TTAK_COLD_PATH tt_autoclean_dirty_pointers(uint64_t now) {
    size_t count = 0;
    void **dirty = tt_inspect_dirty_pointers(now, &count);
    if (!dirty) return;
    for (size_t i = 0; i < count; i++) {
        ttak_mem_free(dirty[i]);
    }
    free(dirty);
}

void TTAK_COLD_PATH **tt_inspect_dirty_pointers(uint64_t now, size_t *count_out) {
    if (!count_out || !global_ptr_map) return NULL;
    *count_out = 0;

    pthread_mutex_lock(&global_map_lock);
    size_t cap = global_ptr_map->cap;
    void **dirty = malloc(sizeof(void *) * global_ptr_map->size);
    if (!dirty) {
        pthread_mutex_unlock(&global_map_lock);
        return NULL;
    }
    size_t found = 0;
    for (size_t i = 0; i < cap; i++) {
        if (global_ptr_map->tbl[i].ctrl == OCCUPIED) {
            void *user_ptr = (void *)global_ptr_map->tbl[i].key;
            ttak_mem_header_t *h = (ttak_mem_header_t *)global_ptr_map->tbl[i].value;
            if ((h->expires_tick != (uint64_t)-1 && now > h->expires_tick) ||
                ttak_atomic_read64(&h->access_count) > 1000000) {
                dirty[found++] = user_ptr;
            }
        }
    }
    pthread_mutex_unlock(&global_map_lock);
    *count_out = found;
    return dirty;
}

void **tt_autoclean_and_inspect(uint64_t now, size_t *count_out) {
    tt_autoclean_dirty_pointers(now);
    return tt_inspect_dirty_pointers(now, count_out);
}

/**
 * @brief "Conservative Mode" pressure sensor for internal schedulers.
 */
_Bool ttak_mem_is_pressure_high(void) {
    return __atomic_load_n(&global_mem_usage, __ATOMIC_SEQ_CST) > TTAK_MEM_HIGH_WATERMARK;
}

/**
 * @brief Atomic WAL pattern for secure state persistence.
 */
void save_current_progress(const char *filename, const void *data, size_t size) {
    char temp_name[256];
    snprintf(temp_name, sizeof(temp_name), "%s.tmp", filename);

    int fd = open(temp_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    if (write(fd, data, size) != (ssize_t)size) {
        close(fd);
        unlink(temp_name);
        return;
    }

    if (fsync(fd) != 0) {
        close(fd);
        unlink(temp_name);
        return;
    }

    close(fd);

    if (rename(temp_name, filename) != 0) {
        unlink(temp_name);
        return;
    }

    // Sync parent directory
    int dfd = open(".", O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }
}
