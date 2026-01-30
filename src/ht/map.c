/**
 * @file map.c
 * @brief Implementation of a hash map using open addressing and SipHash-2-4.
 */

#include <ttak/ht/hash.h>
#include <ttak/ht/map.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>
#include <string.h>

static size_t next_pow2(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
#if UINTPTR_MAX > 0xffffffff
    n |= n >> 32;
#endif
    n++;
    return n;
}

tt_map_t *ttak_create_map(size_t init_cap, uint64_t now) {
    tt_map_t *map = ttak_mem_alloc(sizeof(tt_map_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!map) return NULL;
    
    map->cap = next_pow2(init_cap);
    map->size = 0;
    
    map->tbl = ttak_mem_alloc(map->cap * sizeof(tt_nd_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!map->tbl) {
        ttak_mem_free(map);
        return NULL;
    }
    memset(map->tbl, 0, map->cap * sizeof(tt_nd_t));
    return map;
}

static void ttak_resize_map(tt_map_t *map, uint64_t now) {
    size_t old_cap = map->cap;
    size_t new_cap = old_cap * 2; // Always maintain power of 2
    ttak_node_t *old_tbl = map->tbl;
    
    ttak_node_t *new_tbl = ttak_mem_alloc(new_cap * sizeof(tt_nd_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!new_tbl) return;
    memset(new_tbl, 0, new_cap * sizeof(tt_nd_t));

    map->tbl = new_tbl;
    map->cap = new_cap;
    map->size = 0; // Re-calculate size during insertions

    for (size_t i = 0; i < old_cap; i++) {
        if (old_tbl[i].ctrl == OCCUPIED) {
            ttak_insert_to_map(map, old_tbl[i].key, old_tbl[i].value, now);
        }
    }
    
    ttak_mem_free(old_tbl);
}

void ttak_insert_to_map(tt_map_t *map, uintptr_t key, size_t val, uint64_t now) {
    if (!ttak_mem_access(map, now)) return;
    if (map->size * 10 >= map->cap * 7) {
        ttak_resize_map(map, now);
    }
    uint64_t h   = gen_hash_sip24(key, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);
    size_t   idx = h & (map->cap - 1);
    size_t s_idx = idx;

    while(map->tbl[idx].ctrl == OCCUPIED) {
        if( map->tbl[idx].key == key) {
            map->tbl[idx].value = val;
            return;
        }
        idx = (idx + 1) & (map->cap - 1);
        if (idx == s_idx) return; // Table full
    }

    map->tbl[idx].key   = key;
    map->tbl[idx].value = val;
    map->tbl[idx].ctrl  = OCCUPIED;
    map->size++;
}

_Bool ttak_map_get_key(tt_map_t *map, uintptr_t key, size_t *out, uint64_t now) {
    if (!ttak_mem_access(map, now) || !map->tbl) return 0;
    uint64_t h   = gen_hash_sip24(key, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);
    size_t   idx = h & (map->cap - 1);
    size_t s_idx = idx;

    while (map->tbl[idx].ctrl != EMPTY) {
        if(
           (map->tbl[idx].ctrl == OCCUPIED) &&
           (map->tbl[idx].key == key      ))
        {
            if (out) *out = map->tbl[idx].value;
            return (_Bool)1;
        }
        idx = (idx + 1) & (map->cap - 1);
        if(idx == s_idx) break;
    }
    return (_Bool)0;
}

void ttak_delete_from_map(tt_map_t *map, uintptr_t key, uint64_t now) {
    if (!ttak_mem_access(map, now) || !map->tbl) return;
    uint64_t h   = gen_hash_sip24(key, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);
    size_t   idx = h & (map->cap - 1);
    size_t s_idx = idx;
    _Bool  found = 0;

    while (map->tbl[idx].ctrl != EMPTY) {
        if(
           (map->tbl[idx].ctrl == OCCUPIED) &&
           (map->tbl[idx].key == key      ))
        {
            found = 1;
            break;
        }
        idx = (idx + 1) & (map->cap - 1);
        if(idx == s_idx) break;
    }
    if(!found) return;

    map->tbl[idx].ctrl = DELETED;
    map->size--;

    if (map->size > 0) {
        size_t diff = map->cap / map->size;
        if(diff > 4 && map->cap > 1024) { // Only shrink if very sparse and large
             size_t old_cap = map->cap;
             size_t new_cap = old_cap / 2; 
             if (new_cap >= 8192) {
                 ttak_node_t *old_tbl = map->tbl;
                 ttak_node_t *new_tbl = ttak_mem_alloc(new_cap * sizeof(tt_nd_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
                 if (new_tbl) {
                     memset(new_tbl, 0, new_cap * sizeof(tt_nd_t));
                     map->tbl = new_tbl;
                     map->cap = new_cap;
                     map->size = 0;
                     for (size_t i = 0; i < old_cap; i++) {
                         if (old_tbl[i].ctrl == OCCUPIED) {
                             ttak_insert_to_map(map, old_tbl[i].key, old_tbl[i].value, now);
                         }
                     }
                     ttak_mem_free(old_tbl);
                 }
             }
        }
    }
}