#ifndef VSHAPER_PCAP_REPLAY_H
#define VSHAPER_PCAP_REPLAY_H

#include "common.h"

typedef struct {
    int             fd;
    char            ifname[MAX_IFNAME];
    char            filename[MAX_PATH];
    int             loop_count;
    int             speed_factor;
    unsigned long   total_packets;
    unsigned long   total_bytes;
    int             running;
} pcap_replay_t;

typedef struct {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
} pcap_global_header_t;

typedef struct {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} pcap_packet_header_t;

int  pcap_replay_init(pcap_replay_t *replay, const char *filename,
                       const char *ifname, int loop_count, int speed_factor);
int  pcap_replay_start(pcap_replay_t *replay);
void pcap_replay_stop(pcap_replay_t *replay);
void pcap_replay_destroy(pcap_replay_t *replay);

#endif
