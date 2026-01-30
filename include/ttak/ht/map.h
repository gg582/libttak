#ifndef __TTAK_MAP_H__
#define __TTAK_MAP_H__

#include <stddef.h>
#include <stdint.h>
#include <ttak/ht/hash.h>

// Shortcuts definition
#define ttak_create_map tt_create_map
#define ttak_insert_to_map tt_ins_map
#define ttak_map_get_key tt_map_get
#define ttak_delete_from_map tt_del_map

tt_map_t *ttak_create_map(size_t init_cap, uint64_t now);
void ttak_insert_to_map(tt_map_t *map, uintptr_t key, size_t val, uint64_t now);
void ttak_delete_from_map(tt_map_t *map, uintptr_t key, uint64_t now);
_Bool ttak_map_get_key(tt_map_t *map, uintptr_t key, size_t *out, uint64_t now);

// Macros for memory resizing
#define __TT_MAP_RESIZE__ 3
#define __TT_MAP_SHRINK__ 2

#endif // __TTAK_MAP_H__
