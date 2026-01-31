#include <ttak/tree/ast.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>

void ttak_ast_tree_init(ttak_ast_tree_t *tree, void (*free_value)(void*)) {
    if (!tree) return;
    tree->root = NULL;
    tree->free_value = free_value;
}

ttak_ast_node_t *ttak_ast_create_node(int type, void *value, uint64_t now) {
    ttak_ast_node_t *node = (ttak_ast_node_t *)ttak_mem_alloc(sizeof(ttak_ast_node_t), __TTAK_UNSAFE_MEM_FOREVER__, now);
    if (!node) return NULL;
    
    node->type = type;
    node->value = value;
    node->children = NULL;
    node->num_children = 0;
    node->cap_children = 0;
    node->parent = NULL;
    
    return node;
}

void ttak_ast_add_child(ttak_ast_node_t *parent, ttak_ast_node_t *child, uint64_t now) {
    if (!parent || !child) return;
    
    // Check if we need to resize children array
    if (parent->num_children >= parent->cap_children) {
        size_t new_cap = (parent->cap_children == 0) ? 4 : parent->cap_children * 2;
        ttak_ast_node_t **new_children = (ttak_ast_node_t **)ttak_mem_realloc(
            parent->children, 
            sizeof(ttak_ast_node_t *) * new_cap, 
            __TTAK_UNSAFE_MEM_FOREVER__, 
            now
        );
        if (!new_children) return;
        parent->children = new_children;
        parent->cap_children = new_cap;
    }
    
    parent->children[parent->num_children++] = child;
    child->parent = parent;
}

static void recursive_destroy_node(ttak_ast_node_t *node, void (*free_value)(void*), uint64_t now) {
    if (!node) return;
    
    if (ttak_mem_access(node, now)) {
        for (size_t i = 0; i < node->num_children; i++) {
            recursive_destroy_node(node->children[i], free_value, now);
        }
        
        if (node->children) {
            ttak_mem_free(node->children);
        }
        
        if (free_value && node->value) {
            free_value(node->value);
        }
        
        ttak_mem_free(node);
    }
}

void ttak_ast_tree_destroy(ttak_ast_tree_t *tree, uint64_t now) {
    if (!tree || !tree->root) return;
    recursive_destroy_node(tree->root, tree->free_value, now);
    tree->root = NULL;
}
