#ifndef CAPTURE_H
#define CAPTURE_H

#include "config.h"
#include <pcap/pcap.h>

typedef void (*packet_cb_t)(unsigned char *user,
                            const struct pcap_pkthdr *hdr,
                            const unsigned char *pkt);

typedef struct {
    pcap_t *handle;
    char    errbuf[PCAP_ERRBUF_SIZE];
    int     datalink;
} capture_t;

capture_t *capture_open(const char *iface, int snaplen, int promisc, int timeout_ms,
                        size_t buffer_size);
int        capture_apply_filter(capture_t *c, const char *bpf);
int        capture_loop(capture_t *c, packet_cb_t cb, void *user);
void       capture_close(capture_t *c);
void       capture_stats_print(capture_t *c);

#endif
