#include "config_parser.h"
#include "nvmeof_rpc.h"
#include "bdev_lock.h"
#include "bdev_repl.h"
#include "nvmeof_repl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include "spdk/env.h"
#include "spdk/thread.h"
#include "spdk/log.h"
#include "spdk/conf.h"
#include "spdk/event.h"
#include "spdk/nvme.h"
#include "spdk/bdev.h"
#include "spdk/bdev_module.h"
#include "spdk/bdev_nvme.h"
#include "spdk/nvmf.h"
#include "spdk/nvmf_transport.h"
#include "spdk/jsonrpc.h"
#include "spdk/rpc.h"

static struct spdk_nvmf_tgt *g_tgt;
static struct nvmeof_config g_cfg;
static volatile bool g_running = true;
static struct nvmeof_repl_mgr g_repl_mgr;
static volatile bool g_repl_inited = false;

struct nvmeof_repl_mgr *nvmeof_target_get_repl_mgr(void)
{
    return g_repl_inited ? &g_repl_mgr : NULL;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s -c <config.json> [-m <core_mask>] [-s <rpc_socket>]\n"
        "Options:\n"
        "  -c  path to JSON config file (required)\n"
        "  -m  override core mask (hex)\n"
        "  -s  override RPC socket path\n"
        "  -h  show help\n",
        prog);
}

static void sig_handler(int sig)
{
    (void)sig;
    g_running = false;
    if (g_tgt) spdk_nvmf_tgt_stop_listen(g_tgt);
}

static int create_nvme_bdev(const struct nvmeof_bdev_cfg *b)
{
    struct spdk_nvme_transport_id trid = {};
    if (strcmp(b->trtype, "PCIe") == 0 || strcmp(b->trtype, "pcie") == 0)
        trid.trtype = SPDK_NVME_TRANSPORT_PCIE;
    else {
        SPDK_ERRLOG("Unsupported bdev trtype: %s\n", b->trtype);
        return -1;
    }
    snprintf(trid.traddr, sizeof(trid.traddr), "%s", b->traddr);

    struct spdk_bdev_nvme_opts opts = {};
    spdk_bdev_nvme_get_default_opts(&opts);
    opts.action_on_timeout = SPDK_NVME_CTRLR_TIMEOUT_ACTION_RESET;

    int rc = spdk_bdev_nvme_create(&trid, b->name, NULL, NULL, NULL, &opts);
    if (rc != 0) {
        SPDK_ERRLOG("Failed to create NVMe bdev '%s' at %s: %d\n", b->name, b->traddr, rc);
        return -1;
    }
    SPDK_NOTICELOG("Created NVMe bdev: %s (%s)\n", b->name, b->traddr);
    return 0;
}

static int create_subsystem(const struct nvmeof_subsys_cfg *sc)
{
    struct spdk_nvmf_subsystem *subsys;

    subsys = spdk_nvmf_subsystem_create(g_tgt, sc->nqn, 0, 0);
    if (!subsys) {
        SPDK_ERRLOG("Failed to create subsystem %s\n", sc->nqn);
        return -1;
    }

    spdk_nvmf_subsystem_set_allow_any_host(subsys, sc->allow_any_host);
    for (size_t i = 0; i < sc->host_count; i++) {
        spdk_nvmf_subsystem_add_host(subsys, sc->hosts[i], 0);
    }

    for (size_t i = 0; i < sc->ns_count; i++) {
        const struct nvmeof_ns_cfg *ns = &sc->namespaces[i];
        struct spdk_bdev *raw_bdev = NULL;
        raw_bdev = spdk_bdev_first();
        while (raw_bdev) {
            if (strcmp(spdk_bdev_get_name(raw_bdev), ns->bdev_name) == 0) break;
            raw_bdev = spdk_bdev_next(raw_bdev);
        }
        if (!raw_bdev) {
            SPDK_ERRLOG("Bdev '%s' not found for namespace\n", ns->bdev_name);
            continue;
        }

        char lock_name[128];
        uint32_t use_nsid = ns->nsid > 0 ? ns->nsid : (uint32_t)(i + 1);
        snprintf(lock_name, sizeof(lock_name), "lock_%s_%u", ns->bdev_name, use_nsid);

        int rc = bdev_lock_create(lock_name, ns->bdev_name, ns->enable_barrier);
        if (rc != 0) {
            SPDK_ERRLOG("Failed to create lock bdev '%s' on '%s': %d\n",
                        lock_name, ns->bdev_name, rc);
            continue;
        }

        bool use_repl = ns->enable_replication || sc->replication.enabled;
        const char *final_bdev_name = lock_name;
        char repl_name[128] = {0};

        if (use_repl && sc->replication.role == NVME_REPL_ROLE_PRIMARY_CFG) {
            snprintf(repl_name, sizeof(repl_name), "repl_%s_%u", ns->bdev_name, use_nsid);

            if (!g_repl_inited) {
                nvmeof_repl_mgr_init(&g_repl_mgr, NVME_REPL_ROLE_PRIMARY);
                g_repl_inited = true;
            }

            nvmeof_repl_ns_add(&g_repl_mgr, use_nsid, lock_name);
            for (size_t bi = 0; bi < sc->replication.backup_count; bi++) {
                const struct nvmeof_repl_backup_cfg *bk = &sc->replication.backups[bi];
                nvmeof_repl_backup_add(&g_repl_mgr, use_nsid,
                                       bk->traddr, bk->trsvcid,
                                       bk->nqn,
                                       bk->remote_nsid > 0 ? bk->remote_nsid : use_nsid);
                nvmeof_repl_backup_connect(&g_repl_mgr, use_nsid, bi);
            }

            rc = bdev_repl_create(repl_name, lock_name, use_nsid, &g_repl_mgr);
            if (rc != 0) {
                SPDK_ERRLOG("Failed to create repl bdev '%s': %d\n", repl_name, rc);
            } else {
                final_bdev_name = repl_name;
            }
        }

        struct spdk_bdev *bdev = NULL;
        struct spdk_bdev *it = spdk_bdev_first();
        while (it) {
            if (strcmp(spdk_bdev_get_name(it), final_bdev_name) == 0) { bdev = it; break; }
            it = spdk_bdev_next(it);
        }
        if (!bdev) {
            SPDK_ERRLOG("Final bdev '%s' not found after creation\n", final_bdev_name);
            continue;
        }

        struct spdk_nvmf_ns_opts ns_opts = {};
        size_t opts_size = sizeof(ns_opts);
        spdk_nvmf_ns_get_default_opts(&ns_opts, &opts_size);
        if (ns->nguid[0]) {
            size_t l = strlen(ns->nguid);
            for (size_t k = 0; k < l / 2 && k < 16; k++) {
                unsigned int v;
                sscanf(ns->nguid + 2 * k, "%02x", &v);
                ns_opts.nguid[k] = (uint8_t)v;
            }
        }
        if (use_nsid > 0) ns_opts.nsid = use_nsid;
        uint32_t nsid = spdk_nvmf_subsystem_add_ns(subsys, bdev, &ns_opts, opts_size, NULL);
        if (nsid == 0) {
            SPDK_ERRLOG("Failed to add namespace on %s\n", ns->bdev_name);
        } else {
            SPDK_NOTICELOG("Added nsid=%u on bdev=%s (backend=%s, barrier=%s, repl=%s)\n",
                           nsid, final_bdev_name, ns->bdev_name,
                           ns->enable_barrier ? "on" : "off",
                           use_repl ? "on" : "off");
        }
    }

    for (size_t i = 0; i < sc->listener_count; i++) {
        const struct nvmeof_listener_cfg *l = &sc->listeners[i];
        struct spdk_nvme_transport_id trid = {};
        if (strcmp(l->trtype, "TCP") == 0)
            trid.trtype = SPDK_NVME_TRANSPORT_TCP;
        else if (strcmp(l->trtype, "RDMA") == 0)
            trid.trtype = SPDK_NVME_TRANSPORT_RDMA;
        else {
            SPDK_ERRLOG("Unsupported transport: %s\n", l->trtype);
            continue;
        }
        snprintf(trid.traddr, sizeof(trid.traddr), "%s", l->traddr);
        snprintf(trid.trsvcid, sizeof(trid.trsvcid), "%s", l->trsvcid);
        trid.adrfam = (enum spdk_nvmf_adrfam)l->adrfam;

        struct spdk_nvmf_transport *t = spdk_nvmf_tgt_get_transport(g_tgt, &trid, true);
        if (!t) {
            SPDK_ERRLOG("No transport for %s\n", l->trtype);
            continue;
        }
        int rc = spdk_nvmf_subsystem_add_listener(subsys, &trid);
        if (rc != 0) {
            SPDK_ERRLOG("Failed to add listener %s:%s on %s: %d\n",
                        l->traddr, l->trsvcid, sc->nqn, rc);
        } else {
            SPDK_NOTICELOG("Listener added: %s://%s:%s (%s)\n",
                           l->trtype, l->traddr, l->trsvcid, sc->nqn);
        }
    }

    spdk_nvmf_subsystem_start(subsys);
    return 0;
}

static void start_fn(void *arg1, void *arg2)
{
    (void)arg1; (void)arg2;

    for (size_t i = 0; i < g_cfg.bdev_count; i++) {
        create_nvme_bdev(&g_cfg.bdevs[i]);
    }

    struct spdk_nvmf_target_opts tgt_opts = {};
    snprintf(tgt_opts.name, sizeof(tgt_opts.name), "nvmf_tgt");
    tgt_opts.max_subsystems = 1024;

    g_tgt = spdk_nvmf_tgt_create(&tgt_opts);
    if (!g_tgt) {
        SPDK_ERRLOG("Failed to create NVMf target\n");
        g_running = false;
        return;
    }

    struct spdk_nvmf_transport_opts tcp_opts = {};
    size_t tcp_opts_size = sizeof(tcp_opts);
    spdk_nvmf_tgt_get_transport_opts(g_tgt, "TCP", &tcp_opts, &tcp_opts_size);
    tcp_opts.max_queue_depth = 1024;
    tcp_opts.io_unit_size = 262144;
    tcp_opts.io_queues = 16;
    spdk_nvmf_tgt_set_transport_opts(g_tgt, "TCP", &tcp_opts, tcp_opts_size);

    for (size_t i = 0; i < g_cfg.subsys_count; i++) {
        create_subsystem(&g_cfg.subsystems[i]);
    }

    nvmeof_rpc_register();

    if (g_repl_inited) {
        g_repl_mgr.running = true;
        pthread_t ht;
        if (pthread_create(&ht, NULL, nvmeof_repl_health_thread_fn, &g_repl_mgr) == 0) {
            g_repl_mgr.health_thread = ht;
            SPDK_NOTICELOG("Replication health thread started\n");
        }
    }

    SPDK_NOTICELOG("NVMe-oF target started (rpc_socket=%s)\n", g_cfg.rpc_socket);
}

int main(int argc, char **argv)
{
    const char *config_path = NULL;
    const char *core_mask_override = NULL;
    const char *rpc_override = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "c:m:s:h")) != -1) {
        switch (opt) {
        case 'c': config_path = optarg; break;
        case 'm': core_mask_override = optarg; break;
        case 's': rpc_override = optarg; break;
        case 'h':
        default:
            usage(argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    if (!config_path) {
        usage(argv[0]);
        return 1;
    }

    if (nvmeof_config_load(config_path, &g_cfg) != 0) {
        fprintf(stderr, "Failed to load config: %s\n", config_path);
        return 1;
    }
    if (core_mask_override)
        snprintf(g_cfg.core_mask, sizeof(g_cfg.core_mask), "%s", core_mask_override);
    if (rpc_override)
        snprintf(g_cfg.rpc_socket, sizeof(g_cfg.rpc_socket), "%s", rpc_override);

    struct spdk_env_opts env_opts;
    spdk_env_opts_init(&env_opts);
    env_opts.core_mask = g_cfg.core_mask;
    env_opts.name = "nvmeof_target";
    if (spdk_env_init(&env_opts) < 0) {
        fprintf(stderr, "spdk_env_init failed\n");
        return 1;
    }
    spdk_log_set_level(spdk_log_level_from_str(g_cfg.log_level));
    spdk_log_set_print_level(spdk_log_level_from_str(g_cfg.log_level));

    struct spdk_app_opts app_opts = {};
    spdk_app_opts_init(&app_opts, sizeof(app_opts));
    app_opts.name = "nvmeof_target";
    app_opts.reactor_mask = g_cfg.core_mask;
    app_opts.rpc_addr = g_cfg.rpc_socket;
    app_opts.rpc_mem_size = 32;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    int rc = spdk_app_start(&app_opts, start_fn, NULL, NULL);
    spdk_app_fini();
    return rc;
}
