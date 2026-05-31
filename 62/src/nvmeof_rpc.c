#include "nvmeof_rpc.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "spdk/json.h"
#include "spdk/jsonrpc.h"
#include "spdk/rpc.h"
#include "spdk/log.h"
#include "spdk/string.h"
#include "spdk/bdev.h"
#include "spdk/bdev_nvme.h"
#include "spdk/nvmf.h"

#include "nvmeof_lock.h"
#include "bdev_lock.h"
#include "nvmeof_repl.h"

extern struct spdk_nvmf_tgt *g_tgt;

struct rpc_ns_add {
    char *subsys;
    char *bdev;
    uint32_t nsid;
    char *nguid;
    bool enable_barrier;
};

static const struct spdk_json_object_decoder rpc_ns_add_decoders[] = {
    {"subsys", offsetof(struct rpc_ns_add, subsys), spdk_json_decode_string, true},
    {"bdev", offsetof(struct rpc_ns_add, bdev), spdk_json_decode_string, true},
    {"nsid", offsetof(struct rpc_ns_add, nsid), spdk_json_decode_uint32, false},
    {"nguid", offsetof(struct rpc_ns_add, nguid), spdk_json_decode_string, false},
    {"enable_barrier", offsetof(struct rpc_ns_add, enable_barrier), spdk_json_decode_bool, false},
};

static void rpc_nvmeof_ns_add(struct spdk_jsonrpc_request *request,
                              const struct spdk_json_val *params)
{
    struct rpc_ns_add req = {};
    if (params && spdk_json_decode_object(params, rpc_ns_add_decoders,
                                          SPDK_COUNTOF(rpc_ns_add_decoders), &req) != 0) {
        spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
                                         "Invalid parameters");
        return;
    }

    struct spdk_nvmf_subsystem *subsys = NULL;
    if (g_tgt) {
        struct spdk_nvmf_subsystem *it = spdk_nvmf_subsystem_first(g_tgt);
        while (it) {
            if (strcmp(spdk_nvmf_subsystem_get_nqn(it), req.subsys) == 0) { subsys = it; break; }
            it = spdk_nvmf_subsystem_next(g_tgt, it);
        }
    }
    if (!subsys) {
        spdk_jsonrpc_send_error_response(request, -1, "Subsystem not found");
        return;
    }

    struct spdk_bdev *bdev = spdk_bdev_first();
    while (bdev) {
        if (strcmp(spdk_bdev_get_name(bdev), req.bdev) == 0) break;
        bdev = spdk_bdev_next(bdev);
    }
    if (!bdev) {
        spdk_jsonrpc_send_error_response(request, -2, "Bdev not found");
        return;
    }

    uint32_t use_nsid = req.nsid > 0 ? req.nsid : 0;
    char lock_name[128];
    if (use_nsid > 0)
        snprintf(lock_name, sizeof(lock_name), "lock_%s_%u", req.bdev, use_nsid);
    else
        snprintf(lock_name, sizeof(lock_name), "lock_%s_auto", req.bdev);

    int rc = bdev_lock_create(lock_name, req.bdev, req.enable_barrier);
    if (rc != 0) {
        spdk_jsonrpc_send_error_response(request, rc, "Failed to create lock bdev");
        return;
    }

    struct spdk_bdev *lock_bdev = spdk_bdev_first();
    while (lock_bdev) {
        if (strcmp(spdk_bdev_get_name(lock_bdev), lock_name) == 0) break;
        lock_bdev = spdk_bdev_next(lock_bdev);
    }
    if (!lock_bdev) {
        spdk_jsonrpc_send_error_response(request, -3, "Lock bdev not found");
        return;
    }

    struct spdk_nvmf_ns_opts ns_opts = {};
    size_t ns_opts_size = sizeof(ns_opts);
    spdk_nvmf_ns_get_default_opts(&ns_opts, &ns_opts_size);
    if (req.nguid && req.nguid[0]) {
        size_t l = strlen(req.nguid);
        for (size_t k = 0; k < l / 2 && k < 16; k++) {
            unsigned int v;
            sscanf(req.nguid + 2 * k, "%02x", &v);
            ns_opts.nguid[k] = (uint8_t)v;
        }
    }
    if (use_nsid > 0) ns_opts.nsid = use_nsid;

    uint32_t nsid = spdk_nvmf_subsystem_add_ns(subsys, lock_bdev, &ns_opts, ns_opts_size, NULL);
    if (nsid == 0) {
        spdk_jsonrpc_send_error_response(request, -4, "Failed to add namespace");
        return;
    }

    struct spdk_json_write_ctx *w = spdk_jsonrpc_begin_result(request);
    spdk_json_write_object_begin(w);
    spdk_json_write_named_uint32(w, "nsid", nsid);
    spdk_json_write_object_end(w);
    spdk_jsonrpc_end_result(request, w);
}
SPDK_RPC_REGISTER("nvmeof_ns_add", rpc_nvmeof_ns_add, SPDK_RPC_RUNTIME)

struct rpc_ns_del {
    char *subsys;
    uint32_t nsid;
};

static const struct spdk_json_object_decoder rpc_ns_del_decoders[] = {
    {"subsys", offsetof(struct rpc_ns_del, subsys), spdk_json_decode_string, true},
    {"nsid", offsetof(struct rpc_ns_del, nsid), spdk_json_decode_uint32, true},
};

static void rpc_nvmeof_ns_delete(struct spdk_jsonrpc_request *request,
                                 const struct spdk_json_val *params)
{
    struct rpc_ns_del req = {};
    if (spdk_json_decode_object(params, rpc_ns_del_decoders,
                                SPDK_COUNTOF(rpc_ns_del_decoders), &req) != 0) {
        spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
                                         "Invalid parameters");
        return;
    }
    struct spdk_nvmf_subsystem *subsys = NULL;
    if (g_tgt) {
        struct spdk_nvmf_subsystem *it = spdk_nvmf_subsystem_first(g_tgt);
        while (it) {
            if (strcmp(spdk_nvmf_subsystem_get_nqn(it), req.subsys) == 0) { subsys = it; break; }
            it = spdk_nvmf_subsystem_next(g_tgt, it);
        }
    }
    if (!subsys) {
        spdk_jsonrpc_send_error_response(request, -1, "Subsystem not found");
        return;
    }
    int rc = spdk_nvmf_subsystem_remove_ns(subsys, req.nsid, NULL, NULL);
    if (rc != 0) {
        spdk_jsonrpc_send_error_response(request, rc, "Failed to remove namespace");
        return;
    }
    spdk_jsonrpc_send_bool_response(request, true);
}
SPDK_RPC_REGISTER("nvmeof_ns_delete", rpc_nvmeof_ns_delete, SPDK_RPC_RUNTIME)

static void rpc_nvmeof_initiator_list(struct spdk_jsonrpc_request *request,
                                      const struct spdk_json_val *params)
{
    (void)params;
    struct spdk_json_write_ctx *w = spdk_jsonrpc_begin_result(request);
    spdk_json_write_array_begin(w);
    if (g_tgt) {
        struct spdk_nvmf_subsystem *subsys = spdk_nvmf_subsystem_first(g_tgt);
        while (subsys) {
            struct spdk_nvmf_subsystem_listener *l = spdk_nvmf_subsystem_listener_first(subsys);
            while (l) {
                struct spdk_nvmf_conn *conn = spdk_nvmf_listener_conn_first(l);
                while (conn) {
                    spdk_json_write_object_begin(w);
                    spdk_json_write_named_string(w, "subsystem",
                                                 spdk_nvmf_subsystem_get_nqn(subsys));
                    const struct spdk_nvme_transport_id *trid = spdk_nvmf_conn_get_trid(conn);
                    char trid_str[256];
                    spdk_nvme_trid_populate_transport((struct spdk_nvme_transport_id *)trid,
                                                      trid->trtype);
                    snprintf(trid_str, sizeof(trid_str), "%s://%s:%s",
                             spdk_nvme_transport_id_trtype_str(trid->trtype),
                             trid->traddr, trid->trsvcid);
                    spdk_json_write_named_string(w, "trid", trid_str);
                    spdk_json_write_named_uint32(w, "qid", spdk_nvmf_conn_get_qid(conn));
                    spdk_json_write_object_end(w);
                    conn = spdk_nvmf_listener_conn_next(l, conn);
                }
                l = spdk_nvmf_subsystem_listener_next(subsys, l);
            }
            subsys = spdk_nvmf_subsystem_next(g_tgt, subsys);
        }
    }
    spdk_json_write_array_end(w);
    spdk_jsonrpc_end_result(request, w);
}
SPDK_RPC_REGISTER("nvmeof_initiator_list", rpc_nvmeof_initiator_list, SPDK_RPC_RUNTIME)

static void rpc_nvmeof_io_stats(struct spdk_jsonrpc_request *request,
                                const struct spdk_json_val *params)
{
    (void)params;
    struct spdk_json_write_ctx *w = spdk_jsonrpc_begin_result(request);
    spdk_json_write_array_begin(w);
    if (g_tgt) {
        struct spdk_nvmf_subsystem *subsys = spdk_nvmf_subsystem_first(g_tgt);
        while (subsys) {
            uint32_t num_ns = spdk_nvmf_subsystem_get_max_nsid(subsys);
            for (uint32_t i = 1; i <= num_ns; i++) {
                struct spdk_nvmf_ns *ns = spdk_nvmf_subsystem_get_ns(subsys, i);
                if (!ns) continue;
                struct spdk_bdev *bdev = spdk_nvmf_ns_get_bdev(ns);
                if (!bdev) continue;
                struct spdk_bdev_desc *desc = NULL;
                struct spdk_bdev_io_stat stat = {};
                int rc = spdk_bdev_open_ext(spdk_bdev_get_name(bdev), false, NULL, NULL, &desc);
                if (rc == 0) {
                    spdk_bdev_get_io_stat(desc, &stat);
                    spdk_json_write_object_begin(w);
                    spdk_json_write_named_string(w, "subsystem",
                                                 spdk_nvmf_subsystem_get_nqn(subsys));
                    spdk_json_write_named_uint32(w, "nsid", spdk_nvmf_ns_get_id(ns));
                    spdk_json_write_named_string(w, "bdev", spdk_bdev_get_name(bdev));
                    spdk_json_write_named_uint64(w, "bytes_read", stat.bytes_read);
                    spdk_json_write_named_uint64(w, "bytes_written", stat.bytes_written);
                    spdk_json_write_named_uint64(w, "num_read_ops", stat.num_read_ops);
                    spdk_json_write_named_uint64(w, "num_write_ops", stat.num_write_ops);
                    spdk_json_write_object_end(w);
                    spdk_bdev_close(desc);
                }
            }
            subsys = spdk_nvmf_subsystem_next(g_tgt, subsys);
        }
    }
    spdk_json_write_array_end(w);
    spdk_jsonrpc_end_result(request, w);
}
SPDK_RPC_REGISTER("nvmeof_io_stats", rpc_nvmeof_io_stats, SPDK_RPC_RUNTIME)

static void
nvmeof_lock_visit_write(uint32_t nsid, nvmeof_lock_owner_t owner,
                        uint64_t lba_start, uint64_t lba_count,
                        uint32_t flags, uint64_t age_ms, void *ctx)
{
    struct spdk_json_write_ctx *ww = ctx;
    spdk_json_write_object_begin(ww);
    spdk_json_write_named_uint32(ww, "nsid", nsid);
    spdk_json_write_named_uint64(ww, "owner", owner);
    spdk_json_write_named_uint64(ww, "lba_start", lba_start);
    spdk_json_write_named_uint64(ww, "lba_count", lba_count);
    spdk_json_write_named_uint32(ww, "flags", flags);
    spdk_json_write_named_uint64(ww, "age_ms", age_ms);
    spdk_json_write_object_end(ww);
}

static void rpc_nvmeof_lock_list(struct spdk_jsonrpc_request *request,
                                 const struct spdk_json_val *params)
{
    (void)params;
    struct nvmeof_lock_manager *mgr = bdev_lock_get_mgr();
    struct spdk_json_write_ctx *w = spdk_jsonrpc_begin_result(request);
    spdk_json_write_array_begin(w);
    nvmeof_lock_foreach(mgr, nvmeof_lock_visit_write, w);
    spdk_json_write_array_end(w);
    spdk_jsonrpc_end_result(request, w);
}
SPDK_RPC_REGISTER("nvmeof_lock_list", rpc_nvmeof_lock_list, SPDK_RPC_RUNTIME)

struct rpc_lock_release_all {
    uint32_t nsid;
    uint64_t owner;
};

static const struct spdk_json_object_decoder rpc_lock_release_all_decoders[] = {
    {"nsid", offsetof(struct rpc_lock_release_all, nsid), spdk_json_decode_uint32, true},
    {"owner", offsetof(struct rpc_lock_release_all, owner), spdk_json_decode_uint64, true},
};

static void rpc_nvmeof_lock_release_all(struct spdk_jsonrpc_request *request,
                                        const struct spdk_json_val *params)
{
    struct rpc_lock_release_all req = {};
    if (spdk_json_decode_object(params, rpc_lock_release_all_decoders,
                                SPDK_COUNTOF(rpc_lock_release_all_decoders), &req) != 0) {
        spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
                                         "Invalid parameters");
        return;
    }
    struct nvmeof_lock_manager *mgr = bdev_lock_get_mgr();
    nvmeof_lock_release_all(mgr, req.nsid, req.owner);
    nvmeof_lock_cleanup_expired(mgr);
    spdk_jsonrpc_send_bool_response(request, true);
}
SPDK_RPC_REGISTER("nvmeof_lock_release_all", rpc_nvmeof_lock_release_all, SPDK_RPC_RUNTIME)

static void rpc_nvmeof_lock_cleanup(struct spdk_jsonrpc_request *request,
                                    const struct spdk_json_val *params)
{
    (void)params;
    struct nvmeof_lock_manager *mgr = bdev_lock_get_mgr();
    nvmeof_lock_cleanup_expired(mgr);
    spdk_jsonrpc_send_bool_response(request, true);
}
SPDK_RPC_REGISTER("nvmeof_lock_cleanup", rpc_nvmeof_lock_cleanup, SPDK_RPC_RUNTIME)

static void
nvmeof_repl_visit_write(uint32_t nsid, const char *bdev_name,
                        size_t backup_idx,
                        const struct nvmeof_repl_node *node,
                        uint64_t total_writes, uint64_t failed_writes,
                        uint64_t avg_latency_ns, void *ctx)
{
    struct spdk_json_write_ctx *w = ctx;
    spdk_json_write_object_begin(w);
    spdk_json_write_named_uint32(w, "nsid", nsid);
    spdk_json_write_named_string(w, "bdev", bdev_name);
    spdk_json_write_named_uint32(w, "backup_idx", (uint32_t)backup_idx);
    spdk_json_write_named_string(w, "traddr", node->traddr);
    spdk_json_write_named_string(w, "trsvcid", node->trsvcid);
    spdk_json_write_named_string(w, "nqn", node->nqn);
    spdk_json_write_named_uint32(w, "state", node->state);
    spdk_json_write_named_bool(w, "connected", node->connected);
    spdk_json_write_named_uint32(w, "consecutive_fails", node->consecutive_fails);
    spdk_json_write_named_uint64(w, "total_writes", total_writes);
    spdk_json_write_named_uint64(w, "failed_writes", failed_writes);
    spdk_json_write_named_uint64(w, "avg_latency_ns", avg_latency_ns);
    spdk_json_write_object_end(w);
}

static void rpc_nvmeof_repl_status(struct spdk_jsonrpc_request *request,
                                   const struct spdk_json_val *params)
{
    (void)params;
    struct nvmeof_repl_mgr *mgr = nvmeof_target_get_repl_mgr();
    struct spdk_json_write_ctx *w = spdk_jsonrpc_begin_result(request);
    spdk_json_write_object_begin(w);
    spdk_json_write_named_string(w, "role",
                                 mgr && mgr->role == NVME_REPL_ROLE_PRIMARY ? "primary" : "backup");
    spdk_json_write_named_bool(w, "enabled", mgr ? true : false);
    spdk_json_write_named_array_begin(w, "nodes", 0);
    if (mgr)
        nvmeof_repl_foreach(mgr, nvmeof_repl_visit_write, w);
    spdk_json_write_array_end(w);
    spdk_json_write_object_end(w);
    spdk_jsonrpc_end_result(request, w);
}
SPDK_RPC_REGISTER("nvmeof_repl_status", rpc_nvmeof_repl_status, SPDK_RPC_RUNTIME)

static void rpc_nvmeof_repl_health_check(struct spdk_jsonrpc_request *request,
                                         const struct spdk_json_val *params)
{
    (void)params;
    struct nvmeof_repl_mgr *mgr = nvmeof_target_get_repl_mgr();
    if (mgr)
        nvmeof_repl_health_check(mgr);
    spdk_jsonrpc_send_bool_response(request, true);
}
SPDK_RPC_REGISTER("nvmeof_repl_health_check", rpc_nvmeof_repl_health_check, SPDK_RPC_RUNTIME)

struct rpc_repl_backup_add {
    uint32_t nsid;
    char *traddr;
    char *trsvcid;
    char *nqn;
    uint32_t remote_nsid;
};

static const struct spdk_json_object_decoder rpc_repl_backup_add_decoders[] = {
    {"nsid", offsetof(struct rpc_repl_backup_add, nsid), spdk_json_decode_uint32, true},
    {"traddr", offsetof(struct rpc_repl_backup_add, traddr), spdk_json_decode_string, true},
    {"trsvcid", offsetof(struct rpc_repl_backup_add, trsvcid), spdk_json_decode_string, true},
    {"nqn", offsetof(struct rpc_repl_backup_add, nqn), spdk_json_decode_string, true},
    {"remote_nsid", offsetof(struct rpc_repl_backup_add, remote_nsid), spdk_json_decode_uint32, false},
};

static void rpc_nvmeof_repl_backup_add(struct spdk_jsonrpc_request *request,
                                       const struct spdk_json_val *params)
{
    struct rpc_repl_backup_add req = {};
    if (spdk_json_decode_object(params, rpc_repl_backup_add_decoders,
                                SPDK_COUNTOF(rpc_repl_backup_add_decoders), &req) != 0) {
        spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INVALID_PARAMS,
                                         "Invalid parameters");
        return;
    }
    struct nvmeof_repl_mgr *mgr = nvmeof_target_get_repl_mgr();
    if (!mgr) {
        spdk_jsonrpc_send_error_response(request, -1, "Replication not initialized");
        return;
    }
    int rc = nvmeof_repl_backup_add(mgr, req.nsid, req.traddr, req.trsvcid,
                                    req.nqn,
                                    req.remote_nsid > 0 ? req.remote_nsid : req.nsid);
    if (rc != 0) {
        spdk_jsonrpc_send_error_response(request, rc, "Failed to add backup");
        return;
    }
    spdk_jsonrpc_send_bool_response(request, true);
}
SPDK_RPC_REGISTER("nvmeof_repl_backup_add", rpc_nvmeof_repl_backup_add, SPDK_RPC_RUNTIME)

void nvmeof_rpc_register(void)
{
}
