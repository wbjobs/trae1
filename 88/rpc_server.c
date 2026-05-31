/* SPDX-License-Identifier: BSD-3-Clause
 * RPC Server for ML model hot update and label feedback
 *
 * Simple TCP-based request/response protocol:
 *   Request:  [te_rpc_request]
 *   Response: [te_rpc_response]
 *
 * Commands:
 *   TE_RPC_CMD_LOAD_MODEL : load new ONNX model (hot swap)
 *   TE_RPC_CMD_GET_STATS  : return ML inference statistics
 *   TE_RPC_CMD_SET_LABEL  : attach ground truth label to a flow
 *   TE_RPC_CMD_PING       : health check
 */
#include "te_header.h"
#include <inttypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  RPC server context                                                */
/* ------------------------------------------------------------------ */
static int g_rpc_sock = -1;
static int g_client_sock = -1;
static uint16_t g_rpc_port = TE_ML_RPC_PORT;
static int g_rpc_initialized = 0;

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */
static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ------------------------------------------------------------------ */
/*  RPC command handlers                                              */
/* ------------------------------------------------------------------ */
static int rpc_cmd_load_model(const struct te_rpc_request *req,
                               struct te_rpc_response *resp)
{
    char err_msg[256];
    int rc = te_ml_load_model(req->data.model_path,
                              err_msg, sizeof(err_msg));
    resp->status = rc;
    snprintf(resp->message, sizeof(resp->message),
             "%s", rc == 0 ? "OK" : err_msg);
    return 0;
}

static int rpc_cmd_get_stats(const struct te_rpc_request *req,
                              struct te_rpc_response *resp)
{
    if (!g_te.lcore_stats) {
        resp->status = -1;
        snprintf(resp->message, sizeof(resp->message), "no stats");
        return -1;
    }
    struct te_lcore_stats *s = &g_te.lcore_stats[0];
    double avg_lat_us = s->ml_inference_count > 0 ?
        (double)s->ml_inference_us / (double)s->ml_inference_count : 0.0;
    resp->status = 0;
    snprintf(resp->message, sizeof(resp->message),
             "inferences=%" PRIu64
             " avg_lat=%.2fus max_lat=%" PRIu64
             " fusions=%" PRIu64 " reloads=%" PRIu64,
             s->ml_inference_count,
             avg_lat_us,
             s->ml_latency_max_us,
             s->ml_fusion_count,
             s->ml_model_reloads);
    return 0;
}

static int rpc_cmd_set_label(const struct te_rpc_request *req,
                              struct te_rpc_response *resp)
{
    int rc = te_ml_set_true_label(&req->data.label.key,
                                   req->data.label.true_label);
    resp->status = rc;
    snprintf(resp->message, sizeof(resp->message),
             "%s", rc == 0 ? "label set" : "flow not found");
    return rc;
}

static int rpc_cmd_ping(const struct te_rpc_request *req,
                        struct te_rpc_response *resp)
{
    (void)req;
    resp->status = 0;
    snprintf(resp->message, sizeof(resp->message), "pong");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Process one request                                               */
/* ------------------------------------------------------------------ */
static int rpc_process_one(const struct te_rpc_request *req,
                            struct te_rpc_response *resp)
{
    memset(resp, 0, sizeof(*resp));
    resp->cmd = req->cmd;
    resp->seq = req->seq;
    resp->status = -1;

    switch (req->cmd) {
    case TE_RPC_CMD_LOAD_MODEL:
        return rpc_cmd_load_model(req, resp);
    case TE_RPC_CMD_GET_STATS:
        return rpc_cmd_get_stats(req, resp);
    case TE_RPC_CMD_SET_LABEL:
        return rpc_cmd_set_label(req, resp);
    case TE_RPC_CMD_PING:
        return rpc_cmd_ping(req, resp);
    default:
        snprintf(resp->message, sizeof(resp->message),
                 "unknown cmd=%u", req->cmd);
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/*  Init / Fini                                                       */
/* ------------------------------------------------------------------ */
int te_rpc_init(uint16_t port)
{
    if (g_rpc_initialized)
        return 0;

    g_rpc_port = port;

    g_rpc_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_rpc_sock < 0) {
        RTE_LOG(ERR, USER1, "RPC socket() failed: %s\n", strerror(errno));
        return -1;
    }

    int one = 1;
    if (setsockopt(g_rpc_sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        close(g_rpc_sock);
        g_rpc_sock = -1;
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(g_rpc_port);

    if (bind(g_rpc_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        RTE_LOG(ERR, USER1, "RPC bind(port=%u) failed: %s\n",
                g_rpc_port, strerror(errno));
        close(g_rpc_sock);
        g_rpc_sock = -1;
        return -1;
    }

    if (listen(g_rpc_sock, 4) < 0) {
        close(g_rpc_sock);
        g_rpc_sock = -1;
        return -1;
    }

    if (set_nonblock(g_rpc_sock) < 0) {
        close(g_rpc_sock);
        g_rpc_sock = -1;
        return -1;
    }

    g_rpc_initialized = 1;
    RTE_LOG(INFO, USER1,
            "RPC server listening on 127.0.0.1:%u\n", g_rpc_port);
    return 0;
}

void te_rpc_fini(void)
{
    if (g_client_sock >= 0) {
        close(g_client_sock);
        g_client_sock = -1;
    }
    if (g_rpc_sock >= 0) {
        close(g_rpc_sock);
        g_rpc_sock = -1;
    }
    g_rpc_initialized = 0;
}

/* ------------------------------------------------------------------ */
/*  Periodic processing (non-blocking)                                */
/* ------------------------------------------------------------------ */
void te_rpc_process(void)
{
    if (!g_rpc_initialized || g_rpc_sock < 0)
        return;

    /* Accept new client if not connected */
    if (g_client_sock < 0) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int fd = accept(g_rpc_sock, (struct sockaddr *)&addr, &len);
        if (fd >= 0) {
            set_nonblock(fd);
            g_client_sock = fd;
            RTE_LOG(INFO, USER1, "RPC client connected from %s:%u\n",
                    inet_ntoa(addr.sin_addr),
                    ntohs(addr.sin_port));
        }
    }

    if (g_client_sock < 0)
        return;

    /* Read request */
    struct te_rpc_request req;
    ssize_t nr = recv(g_client_sock, &req, sizeof(req), 0);

    if (nr == 0) {
        /* Client closed */
        close(g_client_sock);
        g_client_sock = -1;
        return;
    }
    if (nr < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            close(g_client_sock);
            g_client_sock = -1;
        }
        return;
    }
    if (nr < (ssize_t)sizeof(req)) {
        /* Partial read, discard */
        return;
    }

    /* Process request */
    struct te_rpc_response resp;
    rpc_process_one(&req, &resp);

    /* Send response */
    ssize_t ns = send(g_client_sock, &resp, sizeof(resp), 0);
    if (ns < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            close(g_client_sock);
            g_client_sock = -1;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Example RPC client utility (compile separate if needed)           */
/* ------------------------------------------------------------------ */
#if 0
/* Example:  ./dpdk_traffic_engine_rpc load_model /path/to/model.onnx */
#include <stdio.h>
int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: %s <cmd> [args]\n", argv[0]); return 1; }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(9876) };
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(fd, (struct sockaddr *)&addr, sizeof(addr));

    struct te_rpc_request req = {0};
    req.seq = 1;
    if (!strcmp(argv[1], "load_model")) {
        req.cmd = TE_RPC_CMD_LOAD_MODEL;
        strncpy(req.data.model_path, argv[2], TE_ML_MODEL_PATH_MAX - 1);
    } else if (!strcmp(argv[1], "stats")) {
        req.cmd = TE_RPC_CMD_GET_STATS;
    } else if (!strcmp(argv[1], "ping")) {
        req.cmd = TE_RPC_CMD_PING;
    } else {
        printf("unknown cmd\n");
        return 1;
    }
    send(fd, &req, sizeof(req), 0);
    struct te_rpc_response resp;
    recv(fd, &resp, sizeof(resp), 0);
    printf("status=%d msg=%s\n", resp.status, resp.message);
    close(fd);
    return 0;
}
#endif
