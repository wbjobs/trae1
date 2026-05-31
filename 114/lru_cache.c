#include "lru_cache.h"
#include <sys/stat.h>
#include <fcntl.h>

static unsigned long hash_key(const char *key, size_t table_size) {
    unsigned long hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % table_size;
}

static void remove_entry_from_list(LruCache *cache, CacheEntry *entry) {
    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        cache->head = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        cache->tail = entry->prev;
    }
    entry->prev = NULL;
    entry->next = NULL;
}

static void add_entry_to_head(LruCache *cache, CacheEntry *entry) {
    entry->next = cache->head;
    entry->prev = NULL;
    if (cache->head) {
        cache->head->prev = entry;
    }
    cache->head = entry;
    if (!cache->tail) {
        cache->tail = entry;
    }
}

static void remove_entry_from_hash(LruCache *cache, CacheEntry *entry) {
    if (entry->hash_prev) {
        entry->hash_prev->hash_next = entry->hash_next;
    } else {
        unsigned long hash = hash_key(entry->key, cache->hash_table_size);
        cache->hash_table[hash] = entry->hash_next;
    }
    if (entry->hash_next) {
        entry->hash_next->hash_prev = entry->hash_prev;
    }
    entry->hash_prev = NULL;
    entry->hash_next = NULL;
}

static void add_entry_to_hash(LruCache *cache, CacheEntry *entry) {
    unsigned long hash = hash_key(entry->key, cache->hash_table_size);
    entry->hash_next = cache->hash_table[hash];
    entry->hash_prev = NULL;
    if (cache->hash_table[hash]) {
        cache->hash_table[hash]->hash_prev = entry;
    }
    cache->hash_table[hash] = entry;
}

static void evict_entry(LruCache *cache) {
    if (!cache->tail) {
        return;
    }

    CacheEntry *entry = cache->tail;
    remove_entry_from_list(cache, entry);
    remove_entry_from_hash(cache, entry);

    cache->current_size -= entry->data_size;
    cache->entry_count--;
    cache->cache_evictions++;

    free(entry->key);
    free(entry->data);
    free(entry);
}

int lru_cache_init(LruCache *cache, size_t max_size, size_t hash_table_size) {
    memset(cache, 0, sizeof(LruCache));
    
    cache->max_size = max_size;
    cache->hash_table_size = hash_table_size > 0 ? hash_table_size : 1024;
    
    cache->hash_table = (CacheEntry **)calloc(cache->hash_table_size, sizeof(CacheEntry *));
    if (!cache->hash_table) {
        return -1;
    }

    if (pthread_mutex_init(&cache->lock, NULL) != 0) {
        free(cache->hash_table);
        return -1;
    }

    return 0;
}

int lru_cache_get(LruCache *cache, const char *key, unsigned char **data, size_t *data_size) {
    if (!key || !data || !data_size) {
        return -1;
    }

    pthread_mutex_lock(&cache->lock);

    unsigned long hash = hash_key(key, cache->hash_table_size);
    CacheEntry *entry = cache->hash_table[hash];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            remove_entry_from_list(cache, entry);
            add_entry_to_head(cache, entry);

            *data = (unsigned char *)malloc(entry->data_size);
            if (*data) {
                memcpy(*data, entry->data, entry->data_size);
                *data_size = entry->data_size;
            }

            entry->last_access = time(NULL);
            entry->access_count++;
            cache->cache_hits++;

            pthread_mutex_unlock(&cache->lock);
            return *data ? 0 : -1;
        }
        entry = entry->hash_next;
    }

    cache->cache_misses++;
    pthread_mutex_unlock(&cache->lock);
    return -1;
}

int lru_cache_put(LruCache *cache, const char *key, const unsigned char *data, 
                  size_t data_size, bool dirty) {
    if (!key || !data || data_size == 0) {
        return -1;
    }

    if (data_size > cache->max_size) {
        return -1;
    }

    pthread_mutex_lock(&cache->lock);

    unsigned long hash = hash_key(key, cache->hash_table_size);
    CacheEntry *entry = cache->hash_table[hash];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            remove_entry_from_list(cache, entry);
            remove_entry_from_hash(cache, entry);

            cache->current_size -= entry->data_size;
            cache->entry_count--;

            free(entry->data);
            free(entry->key);
            free(entry);
            break;
        }
        entry = entry->hash_next;
    }

    while (cache->current_size + data_size > cache->max_size && cache->tail) {
        evict_entry(cache);
    }

    CacheEntry *new_entry = (CacheEntry *)malloc(sizeof(CacheEntry));
    if (!new_entry) {
        pthread_mutex_unlock(&cache->lock);
        return -1;
    }

    new_entry->key = strdup(key);
    new_entry->data = (unsigned char *)malloc(data_size);
    if (!new_entry->key || !new_entry->data) {
        free(new_entry->key);
        free(new_entry->data);
        free(new_entry);
        pthread_mutex_unlock(&cache->lock);
        return -1;
    }

    memcpy(new_entry->data, data, data_size);
    new_entry->data_size = data_size;
    new_entry->dirty = dirty;
    new_entry->last_access = time(NULL);
    new_entry->access_count = 1;
    new_entry->prev = NULL;
    new_entry->next = NULL;
    new_entry->hash_prev = NULL;
    new_entry->hash_next = NULL;

    add_entry_to_hash(cache, new_entry);
    add_entry_to_head(cache, new_entry);

    cache->current_size += data_size;
    cache->entry_count++;

    pthread_mutex_unlock(&cache->lock);
    return 0;
}

int lru_cache_remove(LruCache *cache, const char *key) {
    if (!key) {
        return -1;
    }

    pthread_mutex_lock(&cache->lock);

    unsigned long hash = hash_key(key, cache->hash_table_size);
    CacheEntry *entry = cache->hash_table[hash];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            remove_entry_from_list(cache, entry);
            remove_entry_from_hash(cache, entry);

            cache->current_size -= entry->data_size;
            cache->entry_count--;

            free(entry->key);
            free(entry->data);
            free(entry);

            pthread_mutex_unlock(&cache->lock);
            return 0;
        }
        entry = entry->hash_next;
    }

    pthread_mutex_unlock(&cache->lock);
    return -1;
}

int lru_cache_mark_dirty(LruCache *cache, const char *key) {
    if (!key) {
        return -1;
    }

    pthread_mutex_lock(&cache->lock);

    unsigned long hash = hash_key(key, cache->hash_table_size);
    CacheEntry *entry = cache->hash_table[hash];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            entry->dirty = true;
            pthread_mutex_unlock(&cache->lock);
            return 0;
        }
        entry = entry->hash_next;
    }

    pthread_mutex_unlock(&cache->lock);
    return -1;
}

void lru_cache_flush(LruCache *cache, const char *base_path) {
    if (!base_path) {
        return;
    }

    pthread_mutex_lock(&cache->lock);

    CacheEntry *entry = cache->head;
    while (entry) {
        CacheEntry *next = entry->next;
        if (entry->dirty) {
            char full_path[MAX_PATH_LENGTH];
            snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->key);
            
            int fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                size_t total_written = 0;
                while (total_written < entry->data_size) {
                    ssize_t written = write(fd, entry->data + total_written, 
                                           entry->data_size - total_written);
                    if (written <= 0) break;
                    total_written += (size_t)written;
                }
                close(fd);
                entry->dirty = false;
            }
        }
        entry = next;
    }

    pthread_mutex_unlock(&cache->lock);
}

void lru_cache_stats(LruCache *cache, uint64_t *hits, uint64_t *misses,
                     uint64_t *evictions, size_t *current_size, size_t *entry_count) {
    pthread_mutex_lock(&cache->lock);
    
    if (hits) *hits = cache->cache_hits;
    if (misses) *misses = cache->cache_misses;
    if (evictions) *evictions = cache->cache_evictions;
    if (current_size) *current_size = cache->current_size;
    if (entry_count) *entry_count = cache->entry_count;

    pthread_mutex_unlock(&cache->lock);
}

double lru_cache_hit_rate(LruCache *cache) {
    pthread_mutex_lock(&cache->lock);
    
    uint64_t total = cache->cache_hits + cache->cache_misses;
    double rate = total > 0 ? (double)cache->cache_hits / total * 100.0 : 0.0;

    pthread_mutex_unlock(&cache->lock);
    return rate;
}

void lru_cache_destroy(LruCache *cache) {
    pthread_mutex_lock(&cache->lock);

    CacheEntry *entry = cache->head;
    while (entry) {
        CacheEntry *next = entry->next;
        free(entry->key);
        free(entry->data);
        free(entry);
        entry = next;
    }

    free(cache->hash_table);
    cache->hash_table = NULL;
    cache->head = NULL;
    cache->tail = NULL;
    cache->current_size = 0;
    cache->entry_count = 0;

    pthread_mutex_unlock(&cache->lock);
    pthread_mutex_destroy(&cache->lock);
}
