#include <ttak/ht/table.h>
#include <ttak/mem/mem.h>
#include <stdlib.h>
#include <string.h>

/* SipHash-2-4 implementation for byte arrays */
#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define U8TO64_LE(p) \
    (((uint64_t)((p)[0])) | \
    ((uint64_t)((p)[1]) << 8) | \
    ((uint64_t)((p)[2]) << 16) | \
    ((uint64_t)((p)[3]) << 24) | \
    ((uint64_t)((p)[4]) << 32) | \
    ((uint64_t)((p)[5]) << 40) | \
    ((uint64_t)((p)[6]) << 48) | \
    ((uint64_t)((p)[7]) << 56))

#define SIPROUND \
    do {                    \
        v0 += v1;           \
        v1 = ROTL(v1, 13);  \
        v1 ^= v0;           \
        v0 = ROTL(v0, 32);  \
        v2 += v3;           \
        v3 = ROTL(v3, 16);  \
        v3 ^= v2;           \
        v0 += v3;           \
        v3 = ROTL(v3, 21);  \
        v3 ^= v0;           \
        v2 += v1;           \
        v1 = ROTL(v1, 17);  \
        v1 ^= v2;           \
        v2 = ROTL(v2, 32);  \
    } while (0)

static uint64_t default_siphash(const void *key, size_t len, uint64_t k0, uint64_t k1) {
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;
    const uint8_t *data = (const uint8_t *)key;
    const uint8_t *end = data + (len - (len % 8));
    uint64_t m;

    for (; data != end; data += 8) {
        m = U8TO64_LE(data);
        v3 ^= m;
        SIPROUND;
        SIPROUND;
        v0 ^= m;
    }

    const uint8_t *left = data;
    uint64_t b = ((uint64_t)len) << 56;
    switch (len % 8) {
        /* fall through */
        case 7: b |= ((uint64_t)left[6]) << 48;
        /* fall through */
        case 6: b |= ((uint64_t)left[5]) << 40;
        /* fall through */
        case 5: b |= ((uint64_t)left[4]) << 32;
        /* fall through */
        case 4: b |= ((uint64_t)left[3]) << 24;
        /* fall through */
        case 3: b |= ((uint64_t)left[2]) << 16;
        /* fall through */
        case 2: b |= ((uint64_t)left[1]) << 8;
        /* fall through */
        case 1: b |= ((uint64_t)left[0]); break;
        case 0: break;
    }

    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;

    v2 ^= 0xff;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;

    return v0 ^ v1 ^ v2 ^ v3;
}

void ttak_table_init(ttak_table_t *table, size_t capacity, 
                     uint64_t (*hash_func)(const void*, size_t, uint64_t, uint64_t),
                     int (*key_cmp)(const void*, const void*),
                     void (*key_free)(void*),
                     void (*val_free)(void*)) {
    if (!table) return;
    table->capacity = capacity > 0 ? capacity : 16;
    table->size = 0;
    table->k0 = 0x0706050403020100ULL; // Default keys
    table->k1 = 0x0F0E0D0C0B0A0908ULL;
    table->hash_func = hash_func ? hash_func : default_siphash;
    table->key_cmp = key_cmp;
    table->key_free = key_free;
    table->val_free = val_free;

    // Use huge pages for large tables if possible
    ttak_mem_flags_t flags = (capacity * sizeof(ttak_table_entry_t *) >= 2 * 1024 * 1024) ? TTAK_MEM_HUGE_PAGES : TTAK_MEM_DEFAULT;
    table->buckets = ttak_mem_alloc_safe(sizeof(ttak_table_entry_t *) * table->capacity, __TTAK_UNSAFE_MEM_FOREVER__, 0, false, false, true, true, flags);
    
    // Explicitly zero out buckets (though mem_alloc usually zeros, safe to ensure)
    if (table->buckets) {
        memset(table->buckets, 0, sizeof(ttak_table_entry_t *) * table->capacity);
    }
}

void ttak_table_put(ttak_table_t *table, void *key, size_t key_len, void *value, uint64_t now) {
    if (!table || !table->buckets) return;

    uint64_t hash = table->hash_func(key, key_len, table->k0, table->k1);
    size_t idx = hash % table->capacity;

    ttak_table_entry_t *entry = table->buckets[idx];
    while (entry) {
        if (!ttak_mem_access(entry, now)) {
             // Corruption or stale pointer
             return; 
        }
        if (table->key_cmp(entry->key, key) == 0) {
            // Update value
            if (table->val_free && entry->value) table->val_free(entry->value);
            entry->value = value;
            return;
        }
        entry = entry->next;
    }

    // New entry
    ttak_table_entry_t *new_entry = ttak_mem_alloc_safe(sizeof(ttak_table_entry_t), __TTAK_UNSAFE_MEM_FOREVER__, now, false, false, true, true, TTAK_MEM_DEFAULT);
    if (!new_entry) return;

    new_entry->key = key;
    new_entry->value = value;
    new_entry->next = table->buckets[idx];
    table->buckets[idx] = new_entry;
    table->size++;
}

void *ttak_table_get(ttak_table_t *table, const void *key, size_t key_len, uint64_t now) {
    if (!table || !table->buckets) return NULL;

    uint64_t hash = table->hash_func(key, key_len, table->k0, table->k1);
    size_t idx = hash % table->capacity;

    ttak_table_entry_t *entry = table->buckets[idx];
    while (entry) {
        if (!ttak_mem_access(entry, now)) return NULL;
        if (table->key_cmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}

bool ttak_table_remove(ttak_table_t *table, const void *key, size_t key_len, uint64_t now) {
    if (!table || !table->buckets) return false;

    uint64_t hash = table->hash_func(key, key_len, table->k0, table->k1);
    size_t idx = hash % table->capacity;

    ttak_table_entry_t *entry = table->buckets[idx];
    ttak_table_entry_t *prev = NULL;

    while (entry) {
        if (!ttak_mem_access(entry, now)) return false;
        
        if (table->key_cmp(entry->key, key) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                table->buckets[idx] = entry->next;
            }
            
            if (table->key_free && entry->key) table->key_free(entry->key);
            if (table->val_free && entry->value) table->val_free(entry->value);
            
            ttak_mem_free(entry);
            table->size--;
            return true;
        }
        prev = entry;
        entry = entry->next;
    }
    return false;
}

void ttak_table_destroy(ttak_table_t *table, uint64_t now) {
    if (!table || !table->buckets) return;

    for (size_t i = 0; i < table->capacity; i++) {
        ttak_table_entry_t *entry = table->buckets[i];
        while (entry) {
            ttak_table_entry_t *next = entry->next;
            if (ttak_mem_access(entry, now)) {
                if (table->key_free && entry->key) table->key_free(entry->key);
                if (table->val_free && entry->value) table->val_free(entry->value);
                ttak_mem_free(entry);
            }
            entry = next;
        }
    }
    ttak_mem_free(table->buckets);
    table->size = 0;
}
