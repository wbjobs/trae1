#include "capture.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

capture_t *capture_open(const char *iface, int snaplen, int promisc, int timeout_ms,
                        size_t buffer_size) {
    capture_t *c = calloc(1, sizeof(*c));
    if (!c) die("malloc failed");

    c->handle = pcap_create(iface, c->errbuf);
    if (!c->handle) {
        free(c);
        die("pcap_create(%s): %s", iface ? iface : "default", c->errbuf);
    }

    if (snaplen <= 0) snaplen = SNAP_LEN;
    if (pcap_set_snaplen(c->handle, snaplen) != 0)
        warn("pcap_set_snaplen failed");
    if (pcap_set_promisc(c->handle, promisc) != 0)
        warn("pcap_set_promisc failed");
    if (timeout_ms > 0)
        pcap_set_timeout(c->handle, timeout_ms);

    if (buffer_size > 0)
        pcap_set_buffer_size(c->handle, (int)buffer_size);

    if (pcap_activate(c->handle) != 0) {
        warn("pcap_activate: %s", pcap_geterr(c->handle));
        pcap_close(c->handle);
        free(c);
        return NULL;
    }

    c->datalink = pcap_datalink(c->handle);
    return c;
}

int capture_apply_filter(capture_t *c, const char *bpf) {
    struct bpf_program fp;
    bpf_u_int32 netmask = 0;
    if (pcap_compile(c->handle, &fp, bpf, 0, netmask) == -1) {
        warn("pcap_compile: %s", pcap_geterr(c->handle));
        return -1;
    }
    if (pcap_setfilter(c->handle, &fp) == -1) {
        warn("pcap_setfilter: %s", pcap_geterr(c->handle));
        pcap_freecode(&fp);
        return -1;
    }
    pcap_freecode(&fp);
    return 0;
}

int capture_loop(capture_t *c, packet_cb_t cb, void *user) {
    int ret = pcap_loop(c->handle, -1, (pcap_handler)cb, (u_char *)user);
    if (ret == -1) {
        warn("pcap_loop: %s", pcap_geterr(c->handle));
    }
    return ret;
}

void capture_close(capture_t *c) {
    if (!c) return;
    if (c->handle) pcap_close(c->handle);
    free(c);
}

void capture_stats_print(capture_t *c) {
    struct pcap_stat ps;
    if (pcap_stats(c->handle, &ps) == 0) {
        info("capture stats: received=%u dropped=%u ifdrop=%u",
             ps.ps_recv, ps.ps_drop, ps.ps_ifdrop);
    }
}
