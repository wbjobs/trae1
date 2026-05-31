#include "pcap_replay.h"
#include "dry_run.h"
#include <sys/stat.h>
#include <arpa/inet.h>

#define PCAP_MAGIC      0xa1b2c3d4
#define PCAP_MAGIC_NS   0xa1b23c4d

static int send_raw_packet(const char *ifname, const void *data, size_t len) {
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (sock < 0) {
        fprintf(stderr, "[pcap] raw socket 创建失败: %s\n", strerror(errno));
        return -1;
    }

    int one = 1;
    setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SO_BINDTODEVICE, &ifr) < 0) {
        /* ignore if not supported */
    }

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    if (len >= 20) {
        const unsigned char *ip = (const unsigned char *)data;
        dst.sin_addr.s_addr = (ip[16] << 24) | (ip[17] << 16) |
                              (ip[18] << 8) | ip[19];
    }

    ssize_t sent = sendto(sock, data, len, 0,
                          (struct sockaddr *)&dst, sizeof(dst));
    close(sock);

    if (sent < 0) {
        fprintf(stderr, "[pcap] 发送失败: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static void print_packet_info(unsigned long idx, const unsigned char *data,
                               size_t len, uint32_t ts_sec, uint32_t ts_usec) {
    printf("[pcap] #%-6lu %zu bytes", idx, len);
    if (len >= 14) {
        int ip_start = 14;
        if (len >= ip_start + 20) {
            const unsigned char *ip = data + ip_start;
            unsigned int ver = ip[0] >> 4;
            if (ver == 4) {
                unsigned int src = (ip[12] << 24) | (ip[13] << 16) |
                                   (ip[14] << 8) | ip[15];
                unsigned int dst = (ip[16] << 24) | (ip[17] << 16) |
                                   (ip[18] << 8) | ip[19];
                char src_str[16], dst_str[16];
                snprintf(src_str, sizeof(src_str), "%u.%u.%u.%u",
                         (src >> 24) & 0xFF, (src >> 16) & 0xFF,
                         (src >> 8) & 0xFF, src & 0xFF);
                snprintf(dst_str, sizeof(dst_str), "%u.%u.%u.%u",
                         (dst >> 24) & 0xFF, (dst >> 16) & 0xFF,
                         (dst >> 8) & 0xFF, dst & 0xFF);
                const char *proto = "OTHER";
                switch (ip[9]) {
                case 1:  proto = "ICMP"; break;
                case 6:  proto = "TCP";  break;
                case 17: proto = "UDP";  break;
                }
                printf(" [IPv4 %s %s→%s]", proto, src_str, dst_str);
            }
        }
    }
    printf(" (ts=%u.%06u)\n", ts_sec, ts_usec);
}

int pcap_replay_init(pcap_replay_t *replay, const char *filename,
                       const char *ifname, int loop_count, int speed_factor) {
    if (!replay || !filename || !ifname) return -1;

    memset(replay, 0, sizeof(*replay));
    strncpy(replay->filename, filename, MAX_PATH - 1);
    strncpy(replay->ifname, ifname, MAX_IFNAME - 1);
    replay->loop_count = loop_count > 0 ? loop_count : 1;
    replay->speed_factor = speed_factor > 0 ? speed_factor : 1;
    replay->running = 0;

    struct stat st;
    if (stat(filename, &st) != 0) {
        fprintf(stderr, "[pcap] 文件不存在: %s\n", filename);
        return -1;
    }

    replay->fd = open(filename, O_RDONLY);
    if (replay->fd < 0) {
        fprintf(stderr, "[pcap] 无法打开: %s\n", filename);
        return -1;
    }

    pcap_global_header_t ghdr;
    if (read(replay->fd, &ghdr, sizeof(ghdr)) != sizeof(ghdr)) {
        fprintf(stderr, "[pcap] 读取 global header 失败\n");
        close(replay->fd);
        replay->fd = -1;
        return -1;
    }

    if (ghdr.magic != PCAP_MAGIC && ghdr.magic != PCAP_MAGIC_NS) {
        fprintf(stderr, "[pcap] 无效的 pcap magic: 0x%08x\n", ghdr.magic);
        close(replay->fd);
        replay->fd = -1;
        return -1;
    }

    printf("[pcap] pcap 文件: %s\n", filename);
    printf("[pcap] 版本: %d.%d, 链路类型: %u, 最大长度: %u\n",
           ghdr.version_major, ghdr.version_minor,
           ghdr.network, ghdr.snaplen);
    printf("[pcap] 循环次数: %d, 速度因子: %dx\n",
           replay->loop_count, replay->speed_factor);

    close(replay->fd);
    replay->fd = -1;
    return 0;
}

int pcap_replay_start(pcap_replay_t *replay) {
    if (!replay) return -1;

    replay->running = 1;
    unsigned long seq = 0;

    printf("[pcap] ═══ 开始回放 (Ctrl+C 停止) ═══\n\n");

    dry_run_ctx_t dry_ctx;
    memset(&dry_ctx, 0, sizeof(dry_ctx));
    int dry_inited = 0;

    if (g_config.num_rules > 0 && g_config.dry_run_mode) {
        dry_run_init(&dry_ctx, g_config.ifname, &g_config.rules[0], -1);
        dry_inited = 1;
    }

    for (int loop = 0; loop < replay->loop_count && replay->running && g_running;
         loop++) {
        int fd = open(replay->filename, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "[pcap] 无法重新打开文件\n");
            break;
        }

        pcap_global_header_t ghdr;
        if (read(fd, &ghdr, sizeof(ghdr)) != sizeof(ghdr)) {
            close(fd);
            break;
        }

        uint32_t prev_ts_sec = 0, prev_ts_usec = 0;

        while (replay->running && g_running) {
            pcap_packet_header_t phdr;
            ssize_t n = read(fd, &phdr, sizeof(phdr));
            if (n != sizeof(phdr)) break;

            if (phdr.incl_len > 65536 || phdr.incl_len == 0) {
                fprintf(stderr, "[pcap] 无效的包长度: %u\n", phdr.incl_len);
                break;
            }

            unsigned char *buf = (unsigned char *)malloc(phdr.incl_len);
            if (!buf) break;

            n = read(fd, buf, phdr.incl_len);
            if ((uint32_t)n != phdr.incl_len) {
                free(buf);
                break;
            }

            seq++;
            replay->total_packets++;
            replay->total_bytes += phdr.incl_len;

            print_packet_info(seq, buf, phdr.incl_len,
                              phdr.ts_sec, phdr.ts_usec);

            if (dry_inited) {
                dry_run_process_packet(&dry_ctx, buf, phdr.incl_len, seq);
            } else {
                if (prev_ts_sec > 0) {
                    uint32_t delta_sec = phdr.ts_sec - prev_ts_sec;
                    uint32_t delta_usec;
                    if (phdr.ts_usec >= prev_ts_usec) {
                        delta_usec = phdr.ts_usec - prev_ts_usec;
                    } else {
                        delta_sec--;
                        delta_usec = 1000000 + phdr.ts_usec - prev_ts_usec;
                    }
                    useconds_t sleep_us = (useconds_t)
                        ((delta_sec * 1000000UL + delta_usec) /
                         replay->speed_factor);
                    if (sleep_us > 0 && sleep_us < 5000000) {
                        usleep(sleep_us);
                    }
                }
                prev_ts_sec = phdr.ts_sec;
                prev_ts_usec = phdr.ts_usec;

                if (phdr.incl_len > 14) {
                    send_raw_packet(replay->ifname,
                                    buf + 14, phdr.incl_len - 14);
                }
            }

            free(buf);
        }

        close(fd);
        printf("[pcap] 第 %d/%d 轮回放完成 (%lu 个包)\n",
               loop + 1, replay->loop_count, seq);
    }

    if (dry_inited) {
        dry_run_destroy(&dry_ctx);
    }

    printf("[pcap] ═══ 回放完成 ═══\n");
    printf("[pcap] 总包数: %lu, 总字节数: %lu\n",
           replay->total_packets, replay->total_bytes);

    if (g_config.syslog_enabled) {
        syslog(LOG_INFO, "vshaper: pcap replay done, %lu packets, %lu bytes",
               replay->total_packets, replay->total_bytes);
    }

    return 0;
}

void pcap_replay_stop(pcap_replay_t *replay) {
    if (replay) replay->running = 0;
}

void pcap_replay_destroy(pcap_replay_t *replay) {
    if (!replay) return;
    if (replay->fd >= 0) close(replay->fd);
    memset(replay, 0, sizeof(*replay));
}
