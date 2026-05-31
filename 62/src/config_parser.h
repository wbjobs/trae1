#ifndef NVME_CONFIG_PARSER_H
#define NVME_CONFIG_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NVME_MAX_LISTENERS   8
#define NVME_MAX_NAMESPACES  64
#define NVME_MAX_HOSTS       32
#define NVME_MAX_BDEVS       8
#define NVME_MAX_SUBSYSTEMS  8
#define NVME_MAX_STR         256
#define NVME_REPL_MAX_BACKUPS 4

struct nvmeof_listener_cfg {
    char trtype[16];
    char traddr[64];
    char trsvcid[16];
    int  adrfam;
};

struct nvmeof_ns_cfg {
    char bdev_name[NVME_MAX_STR];
    uint32_t nsid;
    char nguid[33];
    char eui64[17];
    char uuid[37];
    bool enable_barrier;
    bool enable_replication;
};

struct nvmeof_repl_backup_cfg {
    char traddr[64];
    char trsvcid[16];
    char nqn[256];
    uint32_t remote_nsid;
};

struct nvmeof_replication_cfg {
    bool enabled;
    enum { NVME_REPL_ROLE_PRIMARY_CFG = 0, NVME_REPL_ROLE_BACKUP_CFG = 1 } role;
    struct nvmeof_repl_backup_cfg backups[NVME_REPL_MAX_BACKUPS];
    size_t backup_count;
};

struct nvmeof_subsys_cfg {
    char nqn[NVME_MAX_STR];
    bool allow_any_host;
    char hosts[NVME_MAX_HOSTS][NVME_MAX_STR];
    size_t host_count;
    struct nvmeof_ns_cfg namespaces[NVME_MAX_NAMESPACES];
    size_t ns_count;
    struct nvmeof_listener_cfg listeners[NVME_MAX_LISTENERS];
    size_t listener_count;
    struct nvmeof_replication_cfg replication;
};

struct nvmeof_bdev_cfg {
    char name[NVME_MAX_STR];
    char trtype[16];
    char traddr[64];
};

struct nvmeof_config {
    char rpc_socket[NVME_MAX_STR];
    uint32_t hugepage_size_mb;
    char core_mask[32];
    char log_level[16];
    struct nvmeof_bdev_cfg bdevs[NVME_MAX_BDEVS];
    size_t bdev_count;
    struct nvmeof_subsys_cfg subsystems[NVME_MAX_SUBSYSTEMS];
    size_t subsys_count;
};

int nvmeof_config_load(const char *path, struct nvmeof_config *cfg);
void nvmeof_config_defaults(struct nvmeof_config *cfg);

#endif
