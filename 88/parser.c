/* SPDX-License-Identifier: BSD-3-Clause
 * Packet Parser - Ethernet / IPv4 / TCP / UDP zero-copy header walk
 */
#include "te_header.h"

/*
 * Walk mbuf headers in zero-copy manner (no memcpy of payload).
 * Returns 0 on success, -1 on parse error.
 *
 * On success:
 *   - key         : filled 5-tuple
 *   - l4_len      : L4 header length (TCP data offset or UDP 8)
 *   - l4_payload  : pointer into mbuf data at start of L4 payload
 *   - payload_len : remaining payload bytes after L4 header
 */
int te_parse_packet(struct rte_mbuf *mbuf, struct te_5tuple *key,
                    uint16_t *l4_len, uint8_t **l4_payload,
                    uint16_t *payload_len)
{
    struct rte_ether_hdr *eth_hdr;
    struct rte_ipv4_hdr  *ipv4_hdr;
    uint16_t ether_type;
    uint16_t l3_offset = sizeof(struct rte_ether_hdr);
    uint16_t total_len;

    if (unlikely(mbuf == NULL || key == NULL))
        return -1;

    if (unlikely(rte_pktmbuf_data_len(mbuf) < l3_offset))
        return -1;

    eth_hdr = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr *);
    ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);

    /* skip VLAN tags if present (802.1Q/802.1ad) */
    while (ether_type == RTE_ETHER_TYPE_VLAN ||
           ether_type == RTE_ETHER_TYPE_QINQ) {
        l3_offset += 4;
        if (unlikely(rte_pktmbuf_data_len(mbuf) < l3_offset + 2))
            return -1;
        ether_type = rte_be_to_cpu_16(
            *(uint16_t *)(rte_pktmbuf_mtod(mbuf, uint8_t *) + l3_offset - 2));
    }

    if (unlikely(ether_type != RTE_ETHER_TYPE_IPV4))
        return -1;

    if (unlikely(rte_pktmbuf_data_len(mbuf) <
                 l3_offset + sizeof(struct rte_ipv4_hdr)))
        return -1;

    ipv4_hdr = (struct rte_ipv4_hdr *)(
        rte_pktmbuf_mtod(mbuf, uint8_t *) + l3_offset);

    uint8_t ip_ver = ipv4_hdr->version_ihl >> 4;
    if (unlikely(ip_ver != 4))
        return -1;

    uint8_t ihl = (ipv4_hdr->version_ihl & 0x0f) * 4;
    if (unlikely(ihl < sizeof(struct rte_ipv4_hdr)))
        return -1;

    total_len = rte_be_to_cpu_16(ipv4_hdr->total_length);
    if (unlikely(total_len == 0 ||
                 total_len > rte_pktmbuf_data_len(mbuf) - l3_offset))
        total_len = rte_pktmbuf_data_len(mbuf) - l3_offset;

    key->src_ip   = rte_be_to_cpu_32(ipv4_hdr->src_addr);
    key->dst_ip   = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
    key->protocol = ipv4_hdr->next_proto_id;
    key->src_port = 0;
    key->dst_port = 0;

    uint16_t l4_offset = l3_offset + ihl;
    uint16_t l4_total  = total_len - ihl;

    if (key->protocol == IPPROTO_TCP) {
        if (unlikely(l4_total < sizeof(struct rte_tcp_hdr)))
            return -1;
        struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)(
            rte_pktmbuf_mtod(mbuf, uint8_t *) + l4_offset);
        key->src_port = rte_be_to_cpu_16(tcp->src_port);
        key->dst_port = rte_be_to_cpu_16(tcp->dst_port);
        uint16_t tcp_hlen = ((tcp->data_off >> 4) & 0x0f) * 4;
        if (unlikely(tcp_hlen < sizeof(struct rte_tcp_hdr) || tcp_hlen > l4_total))
            return -1;
        *l4_len      = tcp_hlen;
        *l4_payload  = rte_pktmbuf_mtod(mbuf, uint8_t *) + l4_offset + tcp_hlen;
        *payload_len = l4_total - tcp_hlen;

    } else if (key->protocol == IPPROTO_UDP) {
        if (unlikely(l4_total < sizeof(struct rte_udp_hdr)))
            return -1;
        struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(
            rte_pktmbuf_mtod(mbuf, uint8_t *) + l4_offset);
        key->src_port = rte_be_to_cpu_16(udp->src_port);
        key->dst_port = rte_be_to_cpu_16(udp->dst_port);
        *l4_len      = sizeof(struct rte_udp_hdr);
        *l4_payload  = rte_pktmbuf_mtod(mbuf, uint8_t *) +
                       l4_offset + sizeof(struct rte_udp_hdr);
        uint16_t udp_len = rte_be_to_cpu_16(udp->dgram_len);
        if (udp_len >= sizeof(struct rte_udp_hdr) &&
            udp_len <= l4_total)
            *payload_len = udp_len - sizeof(struct rte_udp_hdr);
        else
            *payload_len = l4_total - sizeof(struct rte_udp_hdr);

    } else {
        /* ICMP / other: still return a flow key (ports=0) */
        *l4_len      = 0;
        *l4_payload  = NULL;
        *payload_len = 0;
    }

    return 0;
}
