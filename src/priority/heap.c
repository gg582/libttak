#include <ttak/priority/heap.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>
#include <string.h>

void ttak_heap_tree_init(ttak_heap_tree_t *heap, size_t initial_cap, int (*cmp)(const void*, const void*)) {
    if (!heap) return;
    heap->cmp = cmp;
    heap->size = 0;
    heap->capacity = initial_cap > 0 ? initial_cap : 16;
    heap->data = (void **)ttak_mem_alloc(sizeof(void *) * heap->capacity, __TTAK_UNSAFE_MEM_FOREVER__, 0);
}

static void swap(void **a, void **b) {
    void *temp = *a;
    *a = *b;
    *b = temp;
}

static void heapify_up(ttak_heap_tree_t *heap, size_t index) {
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        // If child violates heap property relative to parent (child > parent for max heap logic if cmp returns a-b)
        // Wait, standard comparator: a < b => negative.
        // Let's assume Min-Heap behavior for "priority queue" defaults usually, or Max.
        // If cmp(child, parent) > 0, we swap. This means "child has higher priority".
        if (heap->cmp(heap->data[index], heap->data[parent]) > 0) {
            swap(&heap->data[index], &heap->data[parent]);
            index = parent;
        } else {
            break;
        }
    }
}

static void heapify_down(ttak_heap_tree_t *heap, size_t index) {
    size_t left, right, largest;
    
    while (true) {
        left = 2 * index + 1;
        right = 2 * index + 2;
        largest = index;

        if (left < heap->size && heap->cmp(heap->data[left], heap->data[largest]) > 0) {
            largest = left;
        }

        if (right < heap->size && heap->cmp(heap->data[right], heap->data[largest]) > 0) {
            largest = right;
        }

        if (largest != index) {
            swap(&heap->data[index], &heap->data[largest]);
            index = largest;
        } else {
            break;
        }
    }
}

void ttak_heap_tree_push(ttak_heap_tree_t *heap, void *element, uint64_t now) {
    if (!heap || !heap->data) return;

    if (heap->size >= heap->capacity) {
        size_t new_cap = heap->capacity * 2;
        void **new_data = (void **)ttak_mem_realloc(heap->data, sizeof(void *) * new_cap, __TTAK_UNSAFE_MEM_FOREVER__, now);
        if (!new_data) return; // Allocation failed
        heap->data = new_data;
        heap->capacity = new_cap;
    }

    heap->data[heap->size] = element;
    heapify_up(heap, heap->size);
    heap->size++;
}

void *ttak_heap_tree_pop(ttak_heap_tree_t *heap, uint64_t now) {
    (void)now; // Access verification not strictly needed for array slots unless checking the array pointer itself
    if (!heap || !heap->data || heap->size == 0) return NULL;

    void *root = heap->data[0];
    heap->data[0] = heap->data[heap->size - 1];
    heap->size--;
    
    if (heap->size > 0) {
        heapify_down(heap, 0);
    }

    return root;
}

void *ttak_heap_tree_peek(const ttak_heap_tree_t *heap) {
    if (!heap || !heap->data || heap->size == 0) return NULL;
    return heap->data[0];
}

void ttak_heap_tree_destroy(ttak_heap_tree_t *heap, uint64_t now) {
    (void)now;
    if (!heap) return;
    if (heap->data) {
        ttak_mem_free(heap->data);
        heap->data = NULL;
    }
    heap->size = 0;
    heap->capacity = 0;
}
