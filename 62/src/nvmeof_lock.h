#ifndef NVME_LOCK_H
#define NVME_LOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#define NVME_LOCK_MAX_REGIONS   256
#define NVME_LOCK_MAX_NAMESPACES 64
#define NVME_LOCK_TIMEOUT_SEC   5

#define NVME_LOCK_FLAG_NONE       0
#define NVME_LOCK_FLAG_WRITE      (1U << 0)
#define NVME_LOCK_FLAG_BARRIER    (1U << 1)

typedef uint64_t nvmeof_lock_owner_t;

struct nvmeof_region_lock {
    nvmeof_lock_owner_t owner;
    uint64_t lba_start;
    uint64_t lba_count;
    uint32_t flags;
    uint64_t acquired_at_ns;
    uint32_t timeout_ms;
    bool active;
};

struct nvmeof_lock_table {
    pthread_mutex_t mu;
    struct nvmeof_region_lock regions[NVME_LOCK_MAX_REGIONS];
    uint32_t count;
    uint32_t nsid;
};

struct nvmeof_lock_manager {
    pthread_mutex_t mu;
    struct nvmeof_lock_table tables[NVME_LOCK_MAX_NAMESPACES];
    uint32_t table_count;
};

void nvmeof_lock_mgr_init(struct nvmeof_lock_manager *mgr);
void nvmeof_lock_mgr_fini(struct nvmeof_lock_manager *mgr);

int  nvmeof_lock_table_add(struct nvmeof_lock_manager *mgr, uint32_t nsid);
void nvmeof_lock_table_remove(struct nvmeof_lock_manager *mgr, uint32_t nsid);

int nvmeof_lock_acquire(struct nvmeof_lock_manager *mgr, uint32_t nsid,
                        nvmeof_lock_owner_t owner,
                        uint64_t lba_start, uint64_t lba_count,
                        uint32_t flags, uint32_t timeout_ms);

int nvmeof_lock_release(struct nvmeof_lock_manager *mgr, uint32_t nsid,
                        nvmeof_lock_owner_t owner,
                        uint64_t lba_start, uint64_t lba_count);

void nvmeof_lock_release_all(struct nvmeof_lock_manager *mgr, uint32_t nsid,
                             nvmeof_lock_owner_t owner);

void nvmeof_lock_cleanup_expired(struct nvmeof_lock_manager *mgr);

typedef void (*nvmeof_lock_visitor_fn)(uint32_t nsid,
                                       nvmeof_lock_owner_t owner,
                                       uint64_t lba_start, uint64_t lba_count,
                                       uint32_t flags, uint64_t age_ms,
                                       void *ctx);

void nvmeof_lock_foreach(struct nvmeof_lock_manager *mgr,
                         nvmeof_lock_visitor_fn fn, void *ctx);

#endif
