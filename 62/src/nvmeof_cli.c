#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "spdk/json.h"
#include "spdk/json_parser.h"

#define RECV_BUF (64 * 1024)

static int connect_rpc(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_all(int fd, const char *buf, size_t len)
{
    while (len) {
        ssize_t n = write(fd, buf, len);
        if (n <= 0) return -1;
        buf += n; len -= (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, char *buf, size_t max)
{
    size_t got = 0;
    while (got < max) {
        ssize_t n = read(fd, buf + got, max - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    buf[got] = '\0';
    return (int)got;
}

static int do_rpc_call(int fd, const char *method, char *params_json,
                       char *resp, size_t resp_max)
{
    char req[8192];
    int n = snprintf(req, sizeof(req),
        "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"id\":1%s%s}\n",
        method, params_json ? ",\"params\":" : "", params_json ? params_json : "");
    if (n < 0 || (size_t)n >= sizeof(req)) return -1;
    if (send_all(fd, req, (size_t)n) != 0) return -1;
    return recv_all(fd, resp, resp_max);
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s -s <rpc_socket> <command> [args]\n"
        "Commands:\n"
        "  initiator list                 List connected initiators\n"
        "  io stats                       Show per-namespace I/O stats (cumulative)\n"
        "  ns add --subsys <nqn> --bdev <name> [--nsid <id>] [--nguid <hex>] [--barrier]\n"
        "  ns delete --subsys <nqn> --nsid <id>\n"
        "  lock list                      List active region locks\n"
        "  lock release_all --nsid <id> --owner <hex>\n"
        "  lock cleanup                   Release all timed-out locks\n"
        "  repl status                    Show replication state & node health\n"
        "  repl health_check              Trigger immediate health check\n"
        "  repl backup_add --nsid <id> --traddr <ip> --trsvcid <port> --nqn <nqn> [--remote_nsid <id>]\n"
        "  sample --nsid <id> --interval <ms> --count <N>   Compute IOPS/BW/lat percentiles\n",
        prog);
}

typedef struct {
    uint64_t bytes_read, bytes_written;
    uint64_t num_read_ops, num_write_ops;
    uint64_t ts_ns;
} stat_sample_t;

static int parse_io_stats(const char *json, uint32_t target_nsid,
                          stat_sample_t *out)
{
    struct spdk_json_val vals[1024];
    int count = spdk_json_parse(json, strlen(json), vals, 1024, NULL, 0);
    if (count < 0) return -1;
    for (int i = 0; i < count; i++) {
        if (vals[i].type != SPDK_JSON_VAL_OBJECT_BEGIN) continue;
        uint32_t nsid = 0;
        uint64_t br = 0, bw = 0, ro = 0, wo = 0;
        int depth = 0;
        for (int j = i + 1; j < count; j++) {
            if (vals[j].type == SPDK_JSON_VAL_OBJECT_BEGIN) depth++;
            if (vals[j].type == SPDK_JSON_VAL_OBJECT_END) {
                if (depth == 0) { if (target_nsid == 0 || nsid == target_nsid) {
                    out->bytes_read = br; out->bytes_written = bw;
                    out->num_read_ops = ro; out->num_write_ops = wo;
                    return 0;
                } break; }
                depth--;
            }
            if (vals[j].type == SPDK_JSON_VAL_NAME && vals[j].len == 4 &&
                memcmp(vals[j].start, "nsid", 4) == 0 && j + 1 < count) {
                nsid = (uint32_t)strtoull(vals[j+1].start, NULL, 10);
            } else if (vals[j].type == SPDK_JSON_VAL_NAME && vals[j].len == 10 &&
                       memcmp(vals[j].start, "bytes_read", 10) == 0 && j + 1 < count) {
                br = strtoull(vals[j+1].start, NULL, 10);
            } else if (vals[j].type == SPDK_JSON_VAL_NAME && vals[j].len == 13 &&
                       memcmp(vals[j].start, "bytes_written", 13) == 0 && j + 1 < count) {
                bw = strtoull(vals[j+1].start, NULL, 10);
            } else if (vals[j].type == SPDK_JSON_VAL_NAME && vals[j].len == 12 &&
                       memcmp(vals[j].start, "num_read_ops", 12) == 0 && j + 1 < count) {
                ro = strtoull(vals[j+1].start, NULL, 10);
            } else if (vals[j].type == SPDK_JSON_VAL_NAME && vals[j].len == 13 &&
                       memcmp(vals[j].start, "num_write_ops", 13) == 0 && j + 1 < count) {
                wo = strtoull(vals[j+1].start, NULL, 10);
            }
        }
    }
    return -1;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

static int do_sample(int fd, uint32_t nsid, int interval_ms, int count)
{
    char resp[RECV_BUF];
    if (do_rpc_call(fd, "nvmeof_io_stats", NULL, resp, sizeof(resp)) < 0) return -1;
    stat_sample_t prev = {}, cur = {};
    parse_io_stats(resp, nsid, &prev);
    prev.ts_ns = now_ns();

    uint64_t *lat_us = calloc(count, sizeof(uint64_t));
    if (!lat_us) return -1;

    printf("%-10s %12s %12s %12s %12s %10s %10s %10s %10s\n",
           "time(s)", "rd_iops", "wr_iops", "rd_mb/s", "wr_mb/s",
           "p50(us)", "p99(us)", "p999(us)", "max(us)");

    for (int i = 0; i < count; i++) {
        usleep(interval_ms * 1000);
        if (do_rpc_call(fd, "nvmeof_io_stats", NULL, resp, sizeof(resp)) < 0) break;
        parse_io_stats(resp, nsid, &cur);
        cur.ts_ns = now_ns();
        double dt = (double)(cur.ts_ns - prev.ts_ns) / 1e9;
        if (dt <= 0) dt = 1e-6;
        uint64_t rd_ops = cur.num_read_ops - prev.num_read_ops;
        uint64_t wr_ops = cur.num_write_ops - prev.num_write_ops;
        uint64_t rd_bytes = cur.bytes_read - prev.bytes_read;
        uint64_t wr_bytes = cur.bytes_written - prev.bytes_written;
        double rd_iops = rd_ops / dt, wr_iops = wr_ops / dt;
        double rd_mb = (double)rd_bytes / dt / (1024 * 1024);
        double wr_mb = (double)wr_bytes / dt / (1024 * 1024);

        uint64_t total_ops = rd_ops + wr_ops;
        uint64_t lat_avg_us = total_ops > 0
            ? (uint64_t)((dt * 1e6) / (double)total_ops) : 0;
        lat_us[i] = lat_avg_us;

        printf("%-10d %12.0f %12.0f %12.1f %12.1f %10" PRIu64 "\n",
               (i + 1) * interval_ms / 1000,
               rd_iops, wr_iops, rd_mb, wr_mb, lat_avg_us);

        prev = cur;
    }

    qsort(lat_us, count, sizeof(uint64_t), cmp_u64);
    uint64_t p50 = lat_us[(size_t)(count * 0.50)];
    uint64_t p99 = lat_us[(size_t)(count * 0.99)];
    uint64_t p999 = lat_us[(size_t)(count * 0.999) < (uint64_t)count
                           ? (size_t)(count * 0.999) : count - 1];
    uint64_t max = lat_us[count - 1];
    printf("\nLatency percentiles (avg-per-interval):\n");
    printf("  p50=%lluus p99=%lluus p99.9=%lluus max=%lluus\n",
           (unsigned long long)p50, (unsigned long long)p99,
           (unsigned long long)p999, (unsigned long long)max);
    free(lat_us);
    return 0;
}

int main(int argc, char **argv)
{
    const char *socket_path = "/var/tmp/nvmeof.sock";
    int opt;
    while ((opt = getopt(argc, argv, "s:h")) != -1) {
        switch (opt) {
        case 's': socket_path = optarg; break;
        case 'h': default:
            usage(argv[0]); return (opt == 'h') ? 0 : 1;
        }
    }
    if (optind >= argc) { usage(argv[0]); return 1; }
    const char *cmd = argv[optind];
    int fd = connect_rpc(socket_path);
    if (fd < 0) { fprintf(stderr, "connect %s failed\n", socket_path); return 1; }

    char resp[RECV_BUF];
    if (strcmp(cmd, "initiator") == 0 && optind + 1 < argc &&
        strcmp(argv[optind + 1], "list") == 0) {
        if (do_rpc_call(fd, "nvmeof_initiator_list", NULL, resp, sizeof(resp)) < 0) return 1;
        printf("%s\n", resp);
    } else if (strcmp(cmd, "io") == 0 && optind + 1 < argc &&
               strcmp(argv[optind + 1], "stats") == 0) {
        if (do_rpc_call(fd, "nvmeof_io_stats", NULL, resp, sizeof(resp)) < 0) return 1;
        printf("%s\n", resp);
    } else if (strcmp(cmd, "ns") == 0 && optind + 1 < argc) {
        const char *sub = NULL, *bdev = NULL, *nguid = NULL;
        uint32_t nsid = 0;
        bool enable_barrier = false;
        for (int i = optind + 2; i < argc; i++) {
            if (strcmp(argv[i], "--subsys") == 0 && i + 1 < argc) sub = argv[++i];
            else if (strcmp(argv[i], "--bdev") == 0 && i + 1 < argc) bdev = argv[++i];
            else if (strcmp(argv[i], "--nsid") == 0 && i + 1 < argc) nsid = (uint32_t)strtoul(argv[++i], NULL, 10);
            else if (strcmp(argv[i], "--nguid") == 0 && i + 1 < argc) nguid = argv[++i];
            else if (strcmp(argv[i], "--barrier") == 0) enable_barrier = true;
        }
        if (strcmp(argv[optind + 1], "add") == 0) {
            if (!sub || !bdev) { usage(argv[0]); return 1; }
            char params[1024];
            int n = snprintf(params, sizeof(params),
                "{\"subsys\":\"%s\",\"bdev\":\"%s\",\"nsid\":%u%s%s%s%s}",
                sub, bdev, nsid,
                nguid ? ",\"nguid\":\"" : "", nguid ? nguid : "",
                nguid ? "\"" : "",
                enable_barrier ? ",\"enable_barrier\":true" : "");
            (void)n;
            if (do_rpc_call(fd, "nvmeof_ns_add", params, resp, sizeof(resp)) < 0) return 1;
            printf("%s\n", resp);
        } else if (strcmp(argv[optind + 1], "delete") == 0) {
            if (!sub || nsid == 0) { usage(argv[0]); return 1; }
            char params[256];
            snprintf(params, sizeof(params), "{\"subsys\":\"%s\",\"nsid\":%u}", sub, nsid);
            if (do_rpc_call(fd, "nvmeof_ns_delete", params, resp, sizeof(resp)) < 0) return 1;
            printf("%s\n", resp);
        } else { usage(argv[0]); return 1; }
    } else if (strcmp(cmd, "lock") == 0 && optind + 1 < argc) {
        if (strcmp(argv[optind + 1], "list") == 0) {
            if (do_rpc_call(fd, "nvmeof_lock_list", NULL, resp, sizeof(resp)) < 0) return 1;
            printf("%s\n", resp);
        } else if (strcmp(argv[optind + 1], "release_all") == 0) {
            uint32_t nsid = 0;
            uint64_t owner = 0;
            for (int i = optind + 2; i < argc; i++) {
                if (strcmp(argv[i], "--nsid") == 0 && i + 1 < argc) nsid = (uint32_t)strtoul(argv[++i], NULL, 10);
                else if (strcmp(argv[i], "--owner") == 0 && i + 1 < argc) owner = strtoull(argv[++i], NULL, 16);
            }
            if (nsid == 0) { usage(argv[0]); return 1; }
            char params[256];
            snprintf(params, sizeof(params), "{\"nsid\":%u,\"owner\":%" PRIu64 "}", nsid, owner);
            if (do_rpc_call(fd, "nvmeof_lock_release_all", params, resp, sizeof(resp)) < 0) return 1;
            printf("%s\n", resp);
        } else if (strcmp(argv[optind + 1], "cleanup") == 0) {
            if (do_rpc_call(fd, "nvmeof_lock_cleanup", NULL, resp, sizeof(resp)) < 0) return 1;
            printf("%s\n", resp);
        } else { usage(argv[0]); return 1; }
    } else if (strcmp(cmd, "repl") == 0 && optind + 1 < argc) {
        if (strcmp(argv[optind + 1], "status") == 0) {
            if (do_rpc_call(fd, "nvmeof_repl_status", NULL, resp, sizeof(resp)) < 0) return 1;
            printf("%s\n", resp);
        } else if (strcmp(argv[optind + 1], "health_check") == 0) {
            if (do_rpc_call(fd, "nvmeof_repl_health_check", NULL, resp, sizeof(resp)) < 0) return 1;
            printf("%s\n", resp);
        } else if (strcmp(argv[optind + 1], "backup_add") == 0) {
            uint32_t nsid = 0, remote_nsid = 0;
            const char *traddr = NULL, *trsvcid = NULL, *nqn = NULL;
            for (int i = optind + 2; i < argc; i++) {
                if (strcmp(argv[i], "--nsid") == 0 && i + 1 < argc) nsid = (uint32_t)strtoul(argv[++i], NULL, 10);
                else if (strcmp(argv[i], "--traddr") == 0 && i + 1 < argc) traddr = argv[++i];
                else if (strcmp(argv[i], "--trsvcid") == 0 && i + 1 < argc) trsvcid = argv[++i];
                else if (strcmp(argv[i], "--nqn") == 0 && i + 1 < argc) nqn = argv[++i];
                else if (strcmp(argv[i], "--remote_nsid") == 0 && i + 1 < argc) remote_nsid = (uint32_t)strtoul(argv[++i], NULL, 10);
            }
            if (!nsid || !traddr || !trsvcid || !nqn) { usage(argv[0]); return 1; }
            char params[1024];
            if (remote_nsid > 0) {
                snprintf(params, sizeof(params),
                         "{\"nsid\":%u,\"traddr\":\"%s\",\"trsvcid\":\"%s\",\"nqn\":\"%s\",\"remote_nsid\":%u}",
                         nsid, traddr, trsvcid, nqn, remote_nsid);
            } else {
                snprintf(params, sizeof(params),
                         "{\"nsid\":%u,\"traddr\":\"%s\",\"trsvcid\":\"%s\",\"nqn\":\"%s\"}",
                         nsid, traddr, trsvcid, nqn);
            }
            if (do_rpc_call(fd, "nvmeof_repl_backup_add", params, resp, sizeof(resp)) < 0) return 1;
            printf("%s\n", resp);
        } else { usage(argv[0]); return 1; }
    } else if (strcmp(cmd, "sample") == 0) {
        uint32_t nsid = 0;
        int interval_ms = 1000, count = 10;
        for (int i = optind + 1; i < argc; i++) {
            if (strcmp(argv[i], "--nsid") == 0 && i + 1 < argc) nsid = (uint32_t)strtoul(argv[++i], NULL, 10);
            else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) interval_ms = atoi(argv[++i]);
            else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) count = atoi(argv[++i]);
        }
        if (do_sample(fd, nsid, interval_ms, count) != 0) return 1;
    } else {
        usage(argv[0]);
        return 1;
    }
    close(fd);
    return 0;
}
