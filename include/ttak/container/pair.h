#ifndef TTAK_CONTAINER_PAIR_H
#define TTAK_CONTAINER_PAIR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Generic Pair/Tuple structure.
 * Despite the name "Pair", it can hold 'length' number of generic elements.
 */
typedef struct ttak_pair {
    size_t length;
    void **elements;
} ttak_pair_t;

/**
 * @brief Initialize a pair (tuple) with a fixed length.
 * @param pair Pointer to the pair structure.
 * @param length Number of elements this pair will hold.
 */
void ttak_pair_init(ttak_pair_t *pair, size_t length, uint64_t now);

/**
 * @brief Set an element at a specific index.
 */
void ttak_pair_set(ttak_pair_t *pair, size_t index, void *element);

/**
 * @brief Get an element at a specific index.
 */
void *ttak_pair_get(const ttak_pair_t *pair, size_t index);

/**
 * @brief Destroy the pair structure.
 * @param free_elem Optional function to free elements.
 */
void ttak_pair_destroy(ttak_pair_t *pair, void (*free_elem)(void*), uint64_t now);

#endif // TTAK_CONTAINER_PAIR_H
