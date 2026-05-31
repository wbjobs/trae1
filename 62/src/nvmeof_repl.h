#ifndef NVME_REPL_H
#define NVME_REPL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "spdk/nvme.h"
#include "spdk/nvme_spec.h"
#include "spdk/bdev.h"

#define NVME_REPL_MAX_BACKUPS  4
#define NVME_REPL_MAX_NS       64
#define NVME_REPL_HEALTH_MS    500
#define NVME_REPL_FAILOVER_MS  3000

enum nvmeof_repl_role {
    NVME_REPL_ROLE_PRIMARY = 0,
    NVME_REPL_ROLE_BACKUP  = 1,
};

enum nvmeof_repl_node_state {
    NVME_REPL_STATE_OFFLINE  = 0,
    NVME_REPL_STATE_CONNECTING,
    NVME_REPL_STATE_HEALTHY,
    NVME_REPL_STATE_DEGRADED,
};

struct nvmeof_repl_node {
    char traddr[64];
    char trsvcid[16];
    char nqn[256];
    enum nvmeof_repl_node_state state;
    uint64_t last_healthy_ts_ns;
    uint64_t last_fail_ts_ns;
    uint32_t consecutive_fails;
    void *ctrlr;
    void *ns;
    uint32_t remote_nsid;
    bool connected;
};

struct nvmeof_repl_ns {
    uint32_t local_nsid;
    char bdev_name[256];
    size_t backup_count;
    struct nvmeof_repl_node backups[NVME_REPL_MAX_BACKUPS];
    pthread_mutex_t mu;
    uint64_t total_writes;
    uint64_t total_write_bytes;
    uint64_t failed_writes;
    uint64_t replicated_bytes;
    uint64_t last_replication_ts_ns;
    uint64_t avg_replication_latency_ns;
};

struct nvmeof_repl_mgr {
    enum nvmeof_repl_role role;
    pthread_t health_thread;
    volatile bool running;
    struct nvmeof_repl_ns nses[NVME_REPL_MAX_NS];
    size_t ns_count;
    pthread_mutex_t mu;
    uint64_t local_node_id;
};

void nvmeof_repl_mgr_init(struct nvmeof_repl_mgr *mgr, enum nvmeof_repl_role role);
void nvmeof_repl_mgr_fini(struct nvmeof_repl_mgr *mgr);

int nvmeof_repl_ns_add(struct nvmeof_repl_mgr *mgr, uint32_t local_nsid,
                       const char *bdev_name);

int nvmeof_repl_backup_add(struct nvmeof_repl_mgr *mgr, uint32_t local_nsid,
                           const char *traddr, const char *trsvcid,
                           const char *nqn, uint32_t remote_nsid);

int nvmeof_repl_backup_connect(struct nvmeof_repl_mgr *mgr, uint32_t local_nsid,
                               size_t backup_idx);

int nvmeof_repl_write(struct nvmeof_repl_mgr *mgr, uint32_t local_nsid,
                      uint64_t lba, uint64_t lba_count, const void *data,
                      uint64_t *completed_mask);

void nvmeof_repl_health_check(struct nvmeof_repl_mgr *mgr);

typedef void (*nvmeof_repl_status_fn)(uint32_t nsid,
                                      const char *bdev_name,
                                      size_t backup_idx,
                                      const struct nvmeof_repl_node *node,
                                      uint64_t total_writes,
                                      uint64_t failed_writes,
                                      uint64_t avg_latency_ns,
                                      void *ctx);

void nvmeof_repl_foreach(struct nvmeof_repl_mgr *mgr,
                         nvmeof_repl_status_fn fn, void *ctx);

void *nvmeof_repl_health_thread_fn(void *arg);

#endif
