#ifndef TTAK_CONTAINER_SET_H
#define TTAK_CONTAINER_SET_H

#include <ttak/ht/table.h>
#include <stdbool.h>

typedef struct ttak_set {
    ttak_table_t table;
} ttak_set_t;

/**
 * @brief Initialize a generic set.
 * @param capacity Initial capacity.
 * @param hash_func Custom hash function (or NULL for default).
 * @param key_cmp Key comparison function.
 * @param key_free Key cleanup function.
 */
void ttak_set_init(ttak_set_t *set, size_t capacity, 
                   uint64_t (*hash_func)(const void*, size_t, uint64_t, uint64_t),
                   int (*key_cmp)(const void*, const void*),
                   void (*key_free)(void*));

/**
 * @brief Add an element to the set.
 */
void ttak_set_add(ttak_set_t *set, void *key, size_t key_len, uint64_t now);

/**
 * @brief Check if the set contains an element.
 */
bool ttak_set_contains(ttak_set_t *set, const void *key, size_t key_len, uint64_t now);

/**
 * @brief Remove an element from the set.
 */
bool ttak_set_remove(ttak_set_t *set, const void *key, size_t key_len, uint64_t now);

/**
 * @brief Destroy the set.
 */
void ttak_set_destroy(ttak_set_t *set, uint64_t now);

#endif // TTAK_CONTAINER_SET_H
