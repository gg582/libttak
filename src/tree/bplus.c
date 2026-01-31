#include <ttak/tree/bplus.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATH 64

static ttak_bplus_node_t *create_node(int order, bool leaf, uint64_t now) {
    ttak_bplus_node_t *node = (ttak_bplus_node_t *)ttak_mem_alloc(sizeof(ttak_bplus_node_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) return NULL;
    
    node->is_leaf = leaf;
    node->n = 0;
    node->next = NULL;
    
    // Allocate max size (order is max children, so max keys = order-1, but we allow order for overflow handling before split)
    // Actually safe implementation allocates order+1 slots to simplify "insert then split".
    int cap = order + 1;
    node->keys = (void **)ttak_mem_alloc(sizeof(void *) * cap, __TTAK_UNSAFE_MEM_FOREVER__, now);
    
    if (leaf) {
        node->values = (void **)ttak_mem_alloc(sizeof(void *) * cap, __TTAK_UNSAFE_MEM_FOREVER__, now);
        node->children = NULL;
    } else {
        node->children = (struct ttak_bplus_node **)ttak_mem_alloc(sizeof(struct ttak_bplus_node *) * (cap + 1), __TTAK_UNSAFE_MEM_FOREVER__, now);
        node->values = NULL;
    }
    
    return node;
}

void ttak_bplus_init(ttak_bplus_tree_t *tree, int order, int (*cmp)(const void*, const void*), void (*kf)(void*), void (*vf)(void*)) {
    if (!tree) return;
    tree->root = NULL;
    tree->order = order < 3 ? 3 : order;
    tree->cmp = cmp;
    tree->key_free = kf;
    tree->val_free = vf;
}

void *ttak_bplus_get(ttak_bplus_tree_t *tree, const void *key, uint64_t now) {
    if (!tree || !tree->root) return NULL;
    ttak_bplus_node_t *c = tree->root;
    
    while (!c->is_leaf) {
        if (!ttak_mem_access(c, now)) return NULL;
        int i = 0;
        while (i < c->n && tree->cmp(key, c->keys[i]) >= 0) {
            i++;
        }
        c = c->children[i];
    }
    
    if (!ttak_mem_access(c, now)) return NULL;
    for (int i = 0; i < c->n; i++) {
        if (tree->cmp(key, c->keys[i]) == 0) {
            return c->values[i];
        }
    }
    return NULL;
}

static void insert_parent(ttak_bplus_tree_t *tree, ttak_bplus_node_t *left, void *key, ttak_bplus_node_t *right, 
                          ttak_bplus_node_t **parents, int parent_idx, uint64_t now) {
    if (parent_idx < 0) {
        // New Root
        ttak_bplus_node_t *root = create_node(tree->order, false, now);
        root->keys[0] = key;
        root->children[0] = left;
        root->children[1] = right;
        root->n = 1;
        tree->root = root;
        return;
    }

    ttak_bplus_node_t *parent = parents[parent_idx];
    int left_index = 0;
    while (left_index <= parent->n && parent->children[left_index] != left) {
        left_index++;
    }
    
    // Shift
    for (int i = parent->n; i > left_index; i--) {
        parent->children[i+1] = parent->children[i];
        parent->keys[i] = parent->keys[i-1];
    }
    parent->keys[left_index] = key;
    parent->children[left_index+1] = right;
    parent->n++;

    if (parent->n >= tree->order) {
        // Split internal
        ttak_bplus_node_t *new_node = create_node(tree->order, false, now);
        int split_idx = (tree->order + 1) / 2; // Bias left? usually order/2.
        // Actually for internal:
        // Keys: [0..split-1] stay, [split] moves up, [split+1..end] move right
        
        int n_left = split_idx; 
        void *up_key = parent->keys[split_idx];
        
        // Move keys/children to new_node
        int j = 0;
        for (int i = split_idx + 1; i < parent->n; i++) {
            new_node->keys[j] = parent->keys[i];
            new_node->children[j] = parent->children[i];
            j++;
        }
        new_node->children[j] = parent->children[parent->n]; // Last child
        new_node->n = j;
        parent->n = n_left;
        
        insert_parent(tree, parent, up_key, new_node, parents, parent_idx - 1, now);
    }
}

void ttak_bplus_insert(ttak_bplus_tree_t *tree, void *key, void *value, uint64_t now) {
    if (!tree) return;
    
    if (!tree->root) {
        tree->root = create_node(tree->order, true, now);
        tree->root->keys[0] = key;
        tree->root->values[0] = value;
        tree->root->n = 1;
        return;
    }

    ttak_bplus_node_t *leaf = tree->root;
    ttak_bplus_node_t *parents[MAX_PATH];
    int p_idx = -1;

    while (!leaf->is_leaf) {
        parents[++p_idx] = leaf;
        int i = 0;
        while (i < leaf->n && tree->cmp(key, leaf->keys[i]) >= 0) {
            i++;
        }
        leaf = leaf->children[i];
    }

    // Insert into leaf
    int i = 0;
    while (i < leaf->n && tree->cmp(key, leaf->keys[i]) > 0) {
        i++;
    }
    
    // Update if exists? Assuming generic map behavior: update
    if (i < leaf->n && tree->cmp(key, leaf->keys[i]) == 0) {
        if (tree->val_free && leaf->values[i]) tree->val_free(leaf->values[i]);
        leaf->values[i] = value;
        // Key might need update if distinct object but same value? assume user handles key.
        return; 
    }

    // Shift
    for (int j = leaf->n; j > i; j--) {
        leaf->keys[j] = leaf->keys[j-1];
        leaf->values[j] = leaf->values[j-1];
    }
    leaf->keys[i] = key;
    leaf->values[i] = value;
    leaf->n++;

    if (leaf->n >= tree->order) {
        // Split leaf
        ttak_bplus_node_t *new_leaf = create_node(tree->order, true, now);
        int split = (tree->order + 1) / 2;
        
        // Move second half
        int j = 0;
        for (int k = split; k < leaf->n; k++) {
            new_leaf->keys[j] = leaf->keys[k];
            new_leaf->values[j] = leaf->values[k];
            j++;
        }
        new_leaf->n = j;
        leaf->n = split;
        
        new_leaf->next = leaf->next;
        leaf->next = new_leaf;
        
        // Copy up middle key (actually first key of right node)
        insert_parent(tree, leaf, new_leaf->keys[0], new_leaf, parents, p_idx, now);
    }
}

static void recursive_destroy(ttak_bplus_node_t *node, void (*kf)(void*), void (*vf)(void*), uint64_t now) {
    if (!node) return;
    if (!node->is_leaf) {
        for (int i = 0; i <= node->n; i++) {
            recursive_destroy(node->children[i], kf, vf, now);
        }
        ttak_mem_free(node->children);
    } else {
        if (vf) {
            for (int i = 0; i < node->n; i++) {
                 vf(node->values[i]);
            }
        }
        ttak_mem_free(node->values);
    }
    
    if (kf) {
        for (int i = 0; i < node->n; i++) {
            kf(node->keys[i]);
        }
    }
    ttak_mem_free(node->keys);
    ttak_mem_free(node);
}

void ttak_bplus_destroy(ttak_bplus_tree_t *tree, uint64_t now) {
    if (!tree || !tree->root) return;
    recursive_destroy(tree->root, tree->key_free, tree->val_free, now);
    tree->root = NULL;
}
