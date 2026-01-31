#include <ttak/container/pair.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>

void ttak_pair_init(ttak_pair_t *pair, size_t length, uint64_t now) {
    if (!pair) return;
    pair->length = length;
    if (length > 0) {
        pair->elements = (void **)ttak_mem_alloc(sizeof(void *) * length, __TTAK_UNSAFE_MEM_FOREVER__, now);
    } else {
        pair->elements = NULL;
    }
}

void ttak_pair_set(ttak_pair_t *pair, size_t index, void *element) {
    if (!pair || !pair->elements || index >= pair->length) return;
    // Access check not strictly required for the array pointer itself if we own it,
    // but good practice if referencing potentially stale pair.
    pair->elements[index] = element;
}

void *ttak_pair_get(const ttak_pair_t *pair, size_t index) {
    if (!pair || !pair->elements || index >= pair->length) return NULL;
    return pair->elements[index];
}

void ttak_pair_destroy(ttak_pair_t *pair, void (*free_elem)(void*), uint64_t now) {
    if (!pair) return;
    
    if (pair->elements) {
        if (ttak_mem_access(pair->elements, now)) {
            if (free_elem) {
                for (size_t i = 0; i < pair->length; i++) {
                    if (pair->elements[i]) {
                        free_elem(pair->elements[i]);
                    }
                }
            }
            ttak_mem_free(pair->elements);
        }
    }
    pair->elements = NULL;
    pair->length = 0;
}
