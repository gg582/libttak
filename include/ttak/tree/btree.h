#ifndef TTAK_TREE_BTREE_H
#define TTAK_TREE_BTREE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct ttak_btree_node {
    int n;          // Current number of keys
    bool leaf;      // Is this a leaf node?
    void **keys;    // Array of keys
    void **values;  // Array of values
    struct ttak_btree_node **children; // Array of child pointers
} ttak_btree_node_t;

typedef struct ttak_btree {
    ttak_btree_node_t *root;
    int t; // Minimum degree (defines range [t-1, 2t-1] keys)
    
    int (*cmp)(const void *k1, const void *k2);
    void (*key_free)(void *key);
    void (*val_free)(void *val);
} ttak_btree_t;

void ttak_btree_init(ttak_btree_t *tree, int t, int (*cmp)(const void*, const void*), void (*key_free)(void*), void (*val_free)(void*));
void ttak_btree_insert(ttak_btree_t *tree, void *key, void *value, uint64_t now);
void *ttak_btree_search(ttak_btree_t *tree, const void *key, uint64_t now);
void ttak_btree_destroy(ttak_btree_t *tree, uint64_t now);

#endif // TTAK_TREE_BTREE_H
