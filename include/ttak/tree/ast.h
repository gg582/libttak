#ifndef TTAK_TREE_AST_H
#define TTAK_TREE_AST_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct ttak_ast_node {
    int type;
    void *value; /* Generic payload */
    
    struct ttak_ast_node **children;
    size_t num_children;
    size_t cap_children;
    
    struct ttak_ast_node *parent;
} ttak_ast_node_t;

typedef struct ttak_ast_tree {
    ttak_ast_node_t *root;
    
    /* Optional hooks for payload management */
    void (*free_value)(void *value);
} ttak_ast_tree_t;

void ttak_ast_tree_init(ttak_ast_tree_t *tree, void (*free_value)(void*));
ttak_ast_node_t *ttak_ast_create_node(int type, void *value, uint64_t now);
void ttak_ast_add_child(ttak_ast_node_t *parent, ttak_ast_node_t *child, uint64_t now);
void ttak_ast_tree_destroy(ttak_ast_tree_t *tree, uint64_t now);

#endif // TTAK_TREE_AST_H
