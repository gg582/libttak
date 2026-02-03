#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <getopt.h>
#include <stdatomic.h>

// libttak includes
#include <ttak/mem/mem.h>
#include <ttak/mem_tree/mem_tree.h>
#include <ttak/ht/map.h>
#include <ttak/sync/sync.h>
#include <ttak/thread/pool.h>
#include <ttak/async/sched.h>
#include <ttak/async/task.h>
#include <ttak/async/future.h>
#include <ttak/timing/timing.h>
#include <ttak/atomic/atomic.h>

// --- Configuration & Defaults ---

typedef struct {
    int num_threads;
    int duration_sec;
    int value_size;
    int keyspace;
    int ttl_ms;
    int epoch_ms;
    double get_ratio;
    double set_ratio;
    double del_ratio;
    int shards;
} config_t;

static config_t cfg = {
    .num_threads = 4,
    .duration_sec = 10,
    .value_size = 256,
    .keyspace = 100000,
    .ttl_ms = 500,
    .epoch_ms = 250,
    .get_ratio = 0.8,
    .set_ratio = 0.19,
    .del_ratio = 0.01,
    .shards = 16
};

// --- Stats ---

typedef struct {
    atomic_uint_fast64_t ops;
    atomic_uint_fast64_t hits;
    atomic_uint_fast64_t misses;
    atomic_uint_fast64_t sets;
    atomic_uint_fast64_t deletes;
    atomic_uint_fast64_t retired;
    atomic_uint_fast64_t rotations;
    atomic_uint_fast64_t cleanup_time_ns;
} stats_t;

static stats_t stats;

// --- Data Structures ---

typedef struct {
    uint64_t epoch_id;
    size_t size;
    void *tree_node; // Pointer to the node in the epoch-specific tree
    char data[]; // Flexible array member
} cache_item_t;

typedef struct {
    uint64_t id;
    ttak_mem_tree_t tree;
    // For each shard, we track keys inserted during this epoch
    struct {
        uintptr_t *keys;
        size_t count;
        size_t capacity;
        ttak_mutex_t lock; // Protects this specific key list
    } shard_keys[];
} epoch_t;

// --- Globals ---

static tt_shard_t *shards;
static epoch_t **epochs; // Ring buffer of epochs
static int max_epochs;
static atomic_uint_fast64_t current_epoch_idx;
static atomic_bool running = true;
static uint64_t start_time_ns;

// --- Helpers ---

static uint64_t now_ns(void) {
    return ttak_get_tick_count_ns();
}

static uint64_t get_ns(void) {
    return ttak_get_tick_count_ns();
}

static long get_rss_kb(void) {
    long rss = 0;
    FILE* fp = fopen("/proc/self/statm", "r");
    if (fp) {
        long resident;
        if (fscanf(fp, "%*s %ld", &resident) == 1) {
            rss = resident * (sysconf(_SC_PAGESIZE) / 1024);
        }
        fclose(fp);
    }
    return rss;
}

// Simple seeded random for deterministic keys
static inline uint32_t fast_rand(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return (uint32_t)x;
}

// Zipf-like distribution approximation
static inline int get_key(uint64_t *state) {
    uint32_t r = fast_rand(state);
    // Simple hot-set: 80% ops on 20% keys
    if ((r % 100) < 80) {
        return (int)(r % (cfg.keyspace / 5));
    } else {
        return (int)(r % cfg.keyspace);
    }
}

static epoch_t *create_epoch(uint64_t id) {
    // Allocate epoch struct + flexible array for shard_keys
    size_t entry_size = sizeof(uintptr_t*) + sizeof(size_t)*2 + sizeof(ttak_mutex_t);
    epoch_t *e = calloc(1, sizeof(epoch_t) + entry_size * (size_t)cfg.shards);

    e->id = id;
    ttak_mem_tree_init(&e->tree);
    ttak_mem_tree_set_manual_cleanup(&e->tree, true); // We trigger cleanup manually

    for (int i = 0; i < cfg.shards; i++) {
        ttak_mutex_init(&e->shard_keys[i].lock);
        e->shard_keys[i].capacity = 1024;
        e->shard_keys[i].keys = malloc(sizeof(uintptr_t) * 1024);
        e->shard_keys[i].count = 0;
    }
    return e;
}

// --- Logic ---

static void record_key_in_epoch(epoch_t *e, int shard_idx, uintptr_t key) {
    ttak_mutex_lock(&e->shard_keys[shard_idx].lock);
    if (e->shard_keys[shard_idx].count == e->shard_keys[shard_idx].capacity) {
        e->shard_keys[shard_idx].capacity *= 2;
        e->shard_keys[shard_idx].keys = realloc(
            e->shard_keys[shard_idx].keys,
            sizeof(uintptr_t) * e->shard_keys[shard_idx].capacity
        );
    }
    e->shard_keys[shard_idx].keys[e->shard_keys[shard_idx].count++] = key;
    ttak_mutex_unlock(&e->shard_keys[shard_idx].lock);
}

static void *worker_task(void *arg) {
    uint64_t seed = (uint64_t)(uintptr_t)arg ^ now_ns();
    char *val_buf = malloc((size_t)cfg.value_size);
    memset(val_buf, 'x', (size_t)cfg.value_size);

    while (atomic_load(&running)) {
        uint32_t r = fast_rand(&seed) % 10000;
        int key_int = get_key(&seed);
        uintptr_t key = (uintptr_t)key_int;
        int shard_idx = key_int % cfg.shards;
        tt_shard_t *s = &shards[shard_idx];
        ttak_map_t *map = (ttak_map_t *)s->data;

        atomic_fetch_add(&stats.ops, 1);

        uint64_t now_ms = ttak_get_tick_count();

        if (r < (uint32_t)(cfg.get_ratio * 10000.0)) {
            // GET
            ttak_rwlock_rdlock(&s->lock);
            size_t val_ptr = 0;
            bool found = ttak_map_get_key(map, key, &val_ptr, now_ms);
            if (found) {
                cache_item_t *item = (cache_item_t*)val_ptr;
                if (item) {
                    volatile char c = item->data[0];
                    (void)c;
                    atomic_fetch_add(&stats.hits, 1);
                } else {
                    atomic_fetch_add(&stats.misses, 1);
                }
            } else {
                atomic_fetch_add(&stats.misses, 1);
            }
            ttak_rwlock_unlock(&s->lock);

        } else if (r < (uint32_t)((cfg.get_ratio + cfg.set_ratio) * 10000.0)) {
            // SET
            uint64_t cur_idx = atomic_load(&current_epoch_idx);
            epoch_t *cur_epoch = epochs[cur_idx % (uint64_t)max_epochs];

            size_t item_size = sizeof(cache_item_t) + (size_t)cfg.value_size;

            // Use a bounded lifetime instead of FOREVER so overwritten items can be reclaimed.
            uint64_t ttl_ms = (uint64_t)cfg.ttl_ms;
            cache_item_t *item = ttak_mem_alloc_safe(item_size, ttl_ms, now_ms, false, false, true, false, TTAK_MEM_DEFAULT);
            if (!item) {
                continue;
            }

            item->epoch_id = cur_epoch->id;
            item->size = (size_t)cfg.value_size;
            memcpy(item->data, val_buf, (size_t)cfg.value_size);

            ttak_mem_tree_add(&cur_epoch->tree, item, item_size, 0, true);

            ttak_rwlock_wrlock(&s->lock);
            ttak_insert_to_map(map, key, (size_t)item, now_ms);
            ttak_rwlock_unlock(&s->lock);

            atomic_fetch_add(&stats.sets, 1);
            record_key_in_epoch(cur_epoch, shard_idx, key);

        } else {
            // DELETE
            ttak_rwlock_wrlock(&s->lock);
            ttak_delete_from_map(map, key, now_ms);
            ttak_rwlock_unlock(&s->lock);
            atomic_fetch_add(&stats.deletes, 1);
        }
    }

    free(val_buf);
    return NULL;
}

static atomic_bool maintenance_active = true;

// Background maintenance
static void *maintenance_task(void *arg) {
    (void)arg;
    uint64_t last_rotation = now_ns();
    uint64_t epoch_ns_val = (uint64_t)cfg.epoch_ms * 1000000ULL;

    while (atomic_load(&running)) {
        uint64_t now = now_ns();
        if (now - last_rotation >= epoch_ns_val) {
            uint64_t start_clean = get_ns();

            uint64_t cur_idx = atomic_load(&current_epoch_idx);
            uint64_t next_idx = cur_idx + 1;
            int ring_idx = (int)(next_idx % (uint64_t)max_epochs);
            epoch_t *target_epoch = epochs[ring_idx];

            if (target_epoch->id != 0) {
                for (int s = 0; s < cfg.shards; s++) {
                    tt_shard_t *shard = &shards[s];
                    ttak_map_t *map = (ttak_map_t *)shard->data;

                    ttak_mutex_lock(&target_epoch->shard_keys[s].lock);
                    size_t count = target_epoch->shard_keys[s].count;
                    uintptr_t *keys = target_epoch->shard_keys[s].keys;

                    ttak_rwlock_wrlock(&shard->lock);
                    for (size_t k = 0; k < count; k++) {
                        size_t val_ptr = 0;
                        if (ttak_map_get_key(map, keys[k], &val_ptr, 0)) {
                            cache_item_t *item = (cache_item_t*)val_ptr;
                            if (item && item->epoch_id == target_epoch->id) {
                                ttak_delete_from_map(map, keys[k], 0);
                                atomic_fetch_add(&stats.retired, 1);
                            }
                        }
                    }
                    ttak_rwlock_unlock(&shard->lock);

                    target_epoch->shard_keys[s].count = 0;
                    ttak_mutex_unlock(&target_epoch->shard_keys[s].lock);
                }

                // Small grace period to reduce immediate reuse pressure.
                usleep(20000);

                ttak_mem_tree_destroy(&target_epoch->tree);
                ttak_mem_tree_init(&target_epoch->tree);
                ttak_mem_tree_set_manual_cleanup(&target_epoch->tree, true);
            }

            target_epoch->id = next_idx;
            atomic_store(&current_epoch_idx, next_idx);

            atomic_fetch_add(&stats.rotations, 1);
            atomic_fetch_add(&stats.cleanup_time_ns, get_ns() - start_clean);
            last_rotation = now;
        }

        ttak_async_yield();
        usleep(10000);
    }
    atomic_store(&maintenance_active, false);
    return NULL;
}

// --- Main ---

void print_stats(void) {
    static uint64_t last_ops = 0;
    static uint64_t last_hits = 0;
    static uint64_t last_rotations = 0;
    static uint64_t last_cleanup_ns = 0;
    static uint64_t last_tick = 0;

    uint64_t now = now_ns();
    if (last_tick == 0) {
        last_tick = now;
        last_ops = atomic_load(&stats.ops);
        last_hits = atomic_load(&stats.hits);
        last_rotations = atomic_load(&stats.rotations);
        last_cleanup_ns = atomic_load(&stats.cleanup_time_ns);
        return;
    }

    uint64_t dt_ns = now - last_tick;
    if (dt_ns == 0) dt_ns = 1;

    uint64_t ops = atomic_load(&stats.ops);
    uint64_t hits = atomic_load(&stats.hits);
    uint64_t rotations = atomic_load(&stats.rotations);
    uint64_t clean_ns = atomic_load(&stats.cleanup_time_ns);
    uint64_t retired = atomic_load(&stats.retired);

    uint64_t dops = ops - last_ops;
    uint64_t dhits = hits - last_hits;
    uint64_t drot = rotations - last_rotations;
    uint64_t dclean = clean_ns - last_cleanup_ns;

    // Instantaneous ops/s over the last sampling window.
    uint64_t ops_per_sec = (dops * 1000000000ULL) / dt_ns;

    // Hit rate for the last sampling window.
    double hit_rate = (dops > 0) ? ((double)dhits * 100.0 / (double)dops) : 0.0;

    // Average cleanup time per rotation for the last sampling window.
    uint64_t clean_ns_avg = (drot > 0) ? (dclean / drot) : 0;

    uint64_t total_items = 0;
    for (int i = 0; i < cfg.shards; i++) {
        tt_shard_t *s = &shards[i];
        ttak_map_t *map = (ttak_map_t *)s->data;
        ttak_rwlock_rdlock(&s->lock);
        total_items += map->size;
        ttak_rwlock_unlock(&s->lock);
    }

    uint64_t elapsed_sec = (now - start_time_ns) / 1000000000ULL;

    printf("STATS: %lu sec | Ops: %lu/s | HitRate: %.2f%% | Epochs: %lu | RSS: %ld KB | Items: %lu | Retired: %lu | CleanNsAvg: %lu\n",
           elapsed_sec,
           ops_per_sec,
           hit_rate,
           rotations,
           get_rss_kb(),
           total_items,
           retired,
           clean_ns_avg);
    fflush(stdout);

    last_ops = ops;
    last_hits = hits;
    last_rotations = rotations;
    last_cleanup_ns = clean_ns;
    last_tick = now;
}

int main(int argc, char **argv) {
    int opt;
    static struct option long_options[] = {
        {"threads", required_argument, 0, 't'},
        {"duration", required_argument, 0, 'd'},
        {"value-size", required_argument, 0, 'v'},
        {"keyspace", required_argument, 0, 'k'},
        {"ttl-ms", required_argument, 0, 'l'},
        {"epoch-ms", required_argument, 0, 'e'},
        {"shards", required_argument, 0, 's'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "t:d:v:k:l:e:s:", long_options, NULL)) != -1) {
        switch (opt) {
            case 't': cfg.num_threads = atoi(optarg); break;
            case 'd': cfg.duration_sec = atoi(optarg); break;
            case 'v': cfg.value_size = atoi(optarg); break;
            case 'k': cfg.keyspace = atoi(optarg); break;
            case 'l': cfg.ttl_ms = atoi(optarg); break;
            case 'e': cfg.epoch_ms = atoi(optarg); break;
            case 's': cfg.shards = atoi(optarg); break;
        }
    }

    printf("Starting TTL Cache Bench\n");
    printf("Threads: %d, Duration: %ds, Value: %dB, Keys: %d, TTL: %dms, Epoch: %dms, Shards: %d\n",
           cfg.num_threads, cfg.duration_sec, cfg.value_size, cfg.keyspace, cfg.ttl_ms, cfg.epoch_ms, cfg.shards);

    ttak_async_init(0);

    // Setup Shards
    shards = calloc((size_t)cfg.shards, sizeof(tt_shard_t));
    for (int i = 0; i < cfg.shards; i++) {
        ttak_map_t *map = ttak_create_map(cfg.keyspace / cfg.shards, ttak_get_tick_count());
        ttak_shard_init(&shards[i], map);
    }

    // Setup Epochs
    int needed_epochs = (cfg.ttl_ms / cfg.epoch_ms) + 4;
    max_epochs = needed_epochs;
    epochs = calloc((size_t)max_epochs, sizeof(epoch_t*));
    for (int i = 0; i < max_epochs; i++) {
        epochs[i] = create_epoch(0);
    }

    epochs[0]->id = 1;
    atomic_store(&current_epoch_idx, 0);

    ttak_thread_pool_t *pool = ttak_thread_pool_create(cfg.num_threads + 1, 0, ttak_get_tick_count());

    start_time_ns = now_ns();

    for (int i = 0; i < cfg.num_threads; i++) {
        ttak_thread_pool_submit_task(pool, worker_task, (void*)(uintptr_t)i, 0, ttak_get_tick_count());
    }

    ttak_thread_pool_submit_task(pool, maintenance_task, NULL, 0, ttak_get_tick_count());

    for (int i = 0; i < cfg.duration_sec; i++) {
        sleep(1);
        print_stats();
    }

    atomic_store(&running, false);
    ttak_thread_pool_destroy(pool);
    ttak_async_shutdown();
    sleep(1);

    print_stats();
    printf("Benchmark Complete.\n");

    return 0;
}
