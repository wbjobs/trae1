#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include "common.h"

typedef struct CacheEntry {
    char *key;
    unsigned char *data;
    size_t data_size;
    struct CacheEntry *prev;
    struct CacheEntry *next;
    struct CacheEntry *hash_prev;
    struct CacheEntry *hash_next;
    bool dirty;
    time_t last_access;
    uint64_t access_count;
} CacheEntry;

typedef struct {
    CacheEntry *head;
    CacheEntry *tail;
    CacheEntry **hash_table;
    size_t hash_table_size;
    size_t max_size;
    size_t current_size;
    size_t entry_count;
    pthread_mutex_t lock;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t cache_evictions;
} LruCache;

int lru_cache_init(LruCache *cache, size_t max_size, size_t hash_table_size);
int lru_cache_get(LruCache *cache, const char *key, unsigned char **data, size_t *data_size);
int lru_cache_put(LruCache *cache, const char *key, const unsigned char *data, size_t data_size, bool dirty);
int lru_cache_remove(LruCache *cache, const char *key);
int lru_cache_mark_dirty(LruCache *cache, const char *key);
void lru_cache_flush(LruCache *cache, const char *base_path);
void lru_cache_stats(LruCache *cache, uint64_t *hits, uint64_t *misses, 
                     uint64_t *evictions, size_t *current_size, size_t *entry_count);
double lru_cache_hit_rate(LruCache *cache);
void lru_cache_destroy(LruCache *cache);

#endif
