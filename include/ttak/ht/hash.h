#ifndef __TTAK_HASH_H__
#define __TTAK_HASH_H__

#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>

#define EMPTY    0x00
#define DELETED  0xDE
#define OCCUPIED 0x0C

/**
 * @brief Node structure with max_align_t padding for Raspberry Pi compatibility.
 */
typedef struct _node {
    uintptr_t key;
    size_t    value;
    uint8_t   ctrl;
    alignas(max_align_t) char padding[0]; 
} ttak_node_t;

typedef ttak_node_t tt_nd_t;

typedef struct {
    ttak_node_t *tbl;
    size_t      cap;
    size_t      size;
    alignas(max_align_t) char padding[0];
} ttak_map_t;

typedef ttak_map_t tt_map_t;

uint64_t gen_hash_sip24(uintptr_t key, uint64_t k0, uint64_t k1);

#endif // __TTAK_HASH_H__