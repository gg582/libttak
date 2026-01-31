#ifndef TTAK_TREE_BPLUS_H
#define TTAK_TREE_BPLUS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct ttak_bplus_node {
    bool is_leaf;
    int n;
    void **keys;
    
    // For internal nodes
    struct ttak_bplus_node **children; 
    
    // For leaf nodes
    void **values;
    struct ttak_bplus_node *next;
} ttak_bplus_node_t;

typedef struct ttak_bplus_tree {
    ttak_bplus_node_t *root;
    int order; // Maximum number of keys? Or degree? Let's use order "m" where max children = m.
               // Max keys = m-1.
    
    int (*cmp)(const void *k1, const void *k2);
    void (*key_free)(void *key);
    void (*val_free)(void *val);
} ttak_bplus_tree_t;

void ttak_bplus_init(ttak_bplus_tree_t *tree, int order, int (*cmp)(const void*, const void*), void (*kf)(void*), void (*vf)(void*));
void ttak_bplus_insert(ttak_bplus_tree_t *tree, void *key, void *value, uint64_t now);
void *ttak_bplus_get(ttak_bplus_tree_t *tree, const void *key, uint64_t now);
void ttak_bplus_destroy(ttak_bplus_tree_t *tree, uint64_t now);

#endif // TTAK_TREE_BPLUS_H
