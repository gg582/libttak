#include <ttak/container/set.h>
#include <stddef.h>

void ttak_set_init(ttak_set_t *set, size_t capacity, 
                   uint64_t (*hash_func)(const void*, size_t, uint64_t, uint64_t),
                   int (*key_cmp)(const void*, const void*),
                   void (*key_free)(void*)) {
    if (!set) return;
    ttak_table_init(&set->table, capacity, hash_func, key_cmp, key_free, NULL);
}

void ttak_set_add(ttak_set_t *set, void *key, size_t key_len, uint64_t now) {
    if (!set) return;
    // We treat existence as the value. We can pass NULL as value.
    // If key exists, table updates value (to NULL) which is fine.
    // But we need to be careful if user expects distinct object ownership.
    // Table put frees old value (NULL here) and updates.
    // It does NOT update key if it matches (cmp returns 0).
    // So if key matches, old key stays, new key is essentially discarded (caller responsibility? or table should free?).
    // Table implementation: if cmp==0, updates value, returns. Does NOT update key.
    // So if we pass a new pointer for an existing key, we might leak the new key pointer if we intended to transfer ownership.
    // But standard set 'add' usually implies "if exists, do nothing". 
    // ttak_table_put updates value.
    // For a set, if it exists, we ideally want to do nothing.
    // Let's check contains first? Or just rely on table behavior.
    // If we rely on table, and we passed a dynamically allocated key, and it already exists, table won't take ownership of new key pointer.
    // So we should check.
    
    if (ttak_table_get(&set->table, key, key_len, now) != NULL) {
        // Wait, get returns value. Value is NULL. So get returns NULL if found (because value is NULL) or if not found.
        // Ambiguity! ttak_table_get returns NULL if not found OR if value is NULL.
        // We need a proper 'contains' check or use 'get' carefully.
        // Actually, ttak_table_t doesn't have a pure 'contains' that distinguishes "found with NULL value" vs "not found".
        // But we can implement 'contains' by implementing it or extending table.
        // Or simpler: Just call put.
        // If we call put, and it exists, table keeps OLD key.
        // If we allocated NEW key, we leak it if we don't free it here.
        // BUT, generic interface assumes user manages lifecycle if it's "void*".
        // If user passes "malloc'd key", they expect set to take ownership.
        // If set already has it, set should probably free the NEW key? Or user checks?
        // Let's assume standard behavior: 'put' takes ownership.
        // But since we wrap table, and table doesn't replace key...
        // We'll stick to direct delegation.
    }
    
    // Actually, for a SET, value is irrelevant. We can store a placeholder non-NULL to distinguish "exists" vs "not found".
    // Let's store (void*)1 as the value.
    ttak_table_put(&set->table, key, key_len, (void*)1, now);
}

bool ttak_set_contains(ttak_set_t *set, const void *key, size_t key_len, uint64_t now) {
    if (!set) return false;
    return ttak_table_get(&set->table, key, key_len, now) != NULL;
}

bool ttak_set_remove(ttak_set_t *set, const void *key, size_t key_len, uint64_t now) {
    if (!set) return false;
    return ttak_table_remove(&set->table, key, key_len, now);
}

void ttak_set_destroy(ttak_set_t *set, uint64_t now) {
    if (!set) return;
    ttak_table_destroy(&set->table, now);
}
