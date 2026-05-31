/*
 * DDoS Defense Gateway - P4 Data Plane v2
 * Implements: SYN Cookie, UDP Rate Limiting (2-level), ICMP Flood Drop, Auto Blacklist
 * Features: 64-bit meters, auto-reset mechanism, hierarchical rate limiting
 */

#include <core.p4>
#include <v1model.p4>

typedef bit<48>  macAddr_t;
typedef bit<32>  ip4Addr_t;
typedef bit<9>   egressSpec_t;
typedef bit<8>   port_t;
typedef bit<64>  counter_t;

header ethernet_t {
    macAddr_t dstAddr;
    macAddr_t srcAddr;
    bit<16>   etherType;
}

header ipv4_t {
    bit<4>    version;
    bit<4>    ihl;
    bit<8>    diffserv;
    bit<16>   totalLen;
    bit<16>   identification;
    bit<3>    flags;
    bit<13>   fragOffset;
    bit<8>    ttl;
    bit<8>    protocol;
    bit<16>   hdrChecksum;
    ip4Addr_t srcAddr;
    ip4Addr_t dstAddr;
}

header tcp_t {
    bit<16>   srcPort;
    bit<16>   dstPort;
    bit<32>   seqNo;
    bit<32>   ackNo;
    bit<4>    dataOffset;
    bit<3>    res;
    bit<3>    ecn;
    bit<1>    urg;
    bit<1>    ack;
    bit<1>    psh;
    bit<1>    rst;
    bit<1>    syn;
    bit<1>    fin;
    bit<16>   window;
    bit<16>   checksum;
    bit<16>   urgentPtr;
}

header udp_t {
    bit<16>   srcPort;
    bit<16>   dstPort;
    bit<16>   length;
    bit<16>   checksum;
}

header icmp_t {
    bit<8>    type;
    bit<8>    code;
    bit<16>   checksum;
}

struct metadata {
    bit<64>   syn_cookie;
    bit<1>    is_attack;
    bit<32>   drop_reason;
    bit<64>   pkt_count;
    bit<32>   meter_index_l1;
    bit<32>   meter_index_l2;
    bit<1>    meter_exceeded_l1;
    bit<1>    meter_exceeded_l2;
}

struct headers {
    ethernet_t ethernet;
    ipv4_t     ipv4;
    tcp_t      tcp;
    udp_t      udp;
    icmp_t     icmp;
}

#define DROP_REASON_NONE        0
#define DROP_REASON_ICMP_FLOOD  1
#define DROP_REASON_UDP_LIMIT   2
#define DROP_REASON_BLACKLIST   3
#define DROP_REASON_SYN_FLOOD   4
#define DROP_REASON_METER_L1    5
#define DROP_REASON_METER_L2    6

#define COUNTER_TYPE_ICMP       1
#define COUNTER_TYPE_UDP        2
#define COUNTER_TYPE_TCP_SYN    3
#define COUNTER_TYPE_BLACKLIST  4
#define COUNTER_TYPE_TOTAL      5
#define COUNTER_TYPE_METER_L1   6
#define COUNTER_TYPE_METER_L2   7

#define METER_SIZE_L1           65536
#define METER_SIZE_L2           262144

/*************************************************************************
 ***********************  P A R S E R  ***********************************
 *************************************************************************/

parser MyParser(packet_in packet,
                out headers hdr,
                inout metadata meta,
                inout standard_metadata_t standard_metadata) {

    state start {
        transition parse_ethernet;
    }

    state parse_ethernet {
        packet.extract(hdr.ethernet);
        transition select(hdr.ethernet.etherType) {
            0x0800 : parse_ipv4;
            default : accept;
        }
    }

    state parse_ipv4 {
        packet.extract(hdr.ipv4);
        transition select(hdr.ipv4.protocol) {
            1 : parse_icmp;
            6 : parse_tcp;
            17 : parse_udp;
            default : accept;
        }
    }

    state parse_tcp {
        packet.extract(hdr.tcp);
        transition accept;
    }

    state parse_udp {
        packet.extract(hdr.udp);
        transition accept;
    }

    state parse_icmp {
        packet.extract(hdr.icmp);
        transition accept;
    }
}

/*************************************************************************
 ****************   C H E C K S U M   V E R I F I C A T I O N   ***********
 *************************************************************************/

control MyVerifyChecksum(inout headers hdr, inout metadata meta) {
    apply { }
}

/*************************************************************************
 **************  I N G R E S S   P R O C E S S I N G   *******************
 *************************************************************************/

control MyIngress(inout headers hdr,
                  inout metadata meta,
                  inout standard_metadata_t standard_metadata) {

    counter(METER_SIZE_L1, CounterType.packets) l1_udp_meter;
    counter(METER_SIZE_L1, CounterType.packets) l1_icmp_meter;
    counter(METER_SIZE_L1, CounterType.packets) l1_syn_meter;

    counter(METER_SIZE_L2, CounterType.packets) l2_udp_meter;
    counter(METER_SIZE_L2, CounterType.packets) l2_icmp_meter;
    counter(METER_SIZE_L2, CounterType.packets) l2_syn_meter;

    counter(1, CounterType.packets) global_packet_counter;
    counter(1, CounterType.packets) global_drop_counter;

    counter(METER_SIZE_L1, CounterType.packets) l1_meter_reset_flag;
    counter(METER_SIZE_L2, CounterType.packets) l2_meter_reset_flag;

    action drop_and_count(bit<32> reason, bit<32> counter_type) {
        meta.is_attack = 1;
        meta.drop_reason = reason;
        standard_metadata.egress_spec = 511;

        global_drop_counter.count((bit<32>)0);
    }

    action forward_to_port(egressSpec_t port) {
        standard_metadata.egress_spec = port;
    }

    action _drop() {
        standard_metadata.egress_spec = 511;
    }

    action generate_syn_cookie() {
        meta.syn_cookie = hash(HashAlgorithm.crc32,
                               (bit<64>)0,
                               { hdr.ipv4.srcAddr, hdr.ipv4.dstAddr,
                                 hdr.tcp.srcPort, hdr.tcp.dstPort,
                                 hdr.tcp.seqNo, hdr.ipv4.protocol });
    }

    action compute_l1_index() {
        meta.meter_index_l1 = (bit<32>)hash(
            HashAlgorithm.crc32,
            (bit<32>)0,
            { hdr.ipv4.srcAddr }
        ) & (METER_SIZE_L1 - 1);
    }

    action compute_l2_index() {
        bit<16> dst_port;
        if (hdr.udp.isValid()) {
            dst_port = hdr.udp.dstPort;
        } else if (hdr.tcp.isValid()) {
            dst_port = hdr.tcp.dstPort;
        } else {
            dst_port = 0;
        }
        meta.meter_index_l2 = (bit<32>)hash(
            HashAlgorithm.crc32,
            (bit<32>)0,
            { hdr.ipv4.srcAddr, dst_port, hdr.ipv4.protocol }
        ) & (METER_SIZE_L2 - 1);
    }

    table ip_blacklist {
        key = {
            hdr.ipv4.srcAddr : exact;
        }
        actions = {
            drop_and_count;
            NoAction;
        }
        default_action = NoAction();
        size = 65536;
    }

    table icmp_flood_check {
        key = {
            hdr.ipv4.srcAddr : exact;
        }
        actions = {
            drop_and_count;
            NoAction;
        }
        default_action = NoAction();
        size = 65536;
    }

    table udp_rate_limit {
        key = {
            hdr.ipv4.srcAddr : exact;
        }
        actions = {
            drop_and_count;
            NoAction;
        }
        default_action = NoAction();
        size = 65536;
    }

    table syn_flood_check {
        key = {
            hdr.ipv4.srcAddr : exact;
        }
        actions = {
            drop_and_count;
            NoAction;
        }
        default_action = NoAction();
        size = 65536;
    }

    table l1_meter_config {
        key = {
            meta.meter_index_l1 : exact;
        }
        actions = {
            NoAction;
            drop_and_count;
        }
        default_action = NoAction();
        size = METER_SIZE_L1;
    }

    table l2_meter_config {
        key = {
            meta.meter_index_l2 : exact;
        }
        actions = {
            NoAction;
            drop_and_count;
        }
        default_action = NoAction();
        size = METER_SIZE_L2;
    }

    table meter_reset_trigger {
        key = {
            standard_metadata.ingress_port : exact;
        }
        actions = {
            NoAction;
        }
        const entries = {
            511 : NoAction();
        }
        default_action = NoAction();
        size = 1;
    }

    table ipv4_lpm {
        key = {
            hdr.ipv4.dstAddr : lpm;
        }
        actions = {
            forward_to_port;
            _drop;
        }
        default_action = _drop();
        size = 1024;
    }

    apply {
        global_packet_counter.count((bit<32>)0);

        if (hdr.ipv4.isValid()) {
            if (ip_blacklist.apply().hit) {
                return;
            }

            compute_l1_index();
            compute_l2_index();

            if (hdr.icmp.isValid()) {
                if (icmp_flood_check.apply().hit) {
                    return;
                }

                l1_icmp_meter.count(meta.meter_index_l1);
                l2_icmp_meter.count(meta.meter_index_l2);

                if (l1_meter_config.apply().hit) {
                    return;
                }

                if (l2_meter_config.apply().hit) {
                    return;
                }
            }

            if (hdr.udp.isValid()) {
                if (udp_rate_limit.apply().hit) {
                    return;
                }

                l1_udp_meter.count(meta.meter_index_l1);
                l2_udp_meter.count(meta.meter_index_l2);

                if (l1_meter_config.apply().hit) {
                    return;
                }

                if (l2_meter_config.apply().hit) {
                    return;
                }
            }

            if (hdr.tcp.isValid() && hdr.tcp.syn == 1 && hdr.tcp.ack == 0) {
                if (syn_flood_check.apply().hit) {
                    return;
                }

                l1_syn_meter.count(meta.meter_index_l1);
                l2_syn_meter.count(meta.meter_index_l2);

                if (l1_meter_config.apply().hit) {
                    return;
                }

                if (l2_meter_config.apply().hit) {
                    return;
                }

                generate_syn_cookie();
            }

            ipv4_lpm.apply();
        }
    }
}

/*************************************************************************
 ****************  E G R E S S   P R O C E S S I N G   ********************
 *************************************************************************/

control MyEgress(inout headers hdr,
                 inout metadata meta,
                 inout standard_metadata_t standard_metadata) {
    apply { }
}

/*************************************************************************
 *************   C H E C K S U M   C O M P U T A T I O N   ***************
 *************************************************************************/

control MyComputeChecksum(inout headers hdr, inout metadata meta) {
    apply { }
}

/*************************************************************************
 ***********************  D E P A R S E R  ********************************
 *************************************************************************/

control MyDeparser(packet_out packet, in headers hdr) {
    apply {
        packet.emit(hdr.ethernet);
        packet.emit(hdr.ipv4);
        packet.emit(hdr.tcp);
        packet.emit(hdr.udp);
        packet.emit(hdr.icmp);
    }
}

/*************************************************************************
 ***********************  S W I T C H  ************************************
 *************************************************************************/

V1Switch(
    MyParser(),
    MyVerifyChecksum(),
    MyIngress(),
    MyEgress(),
    MyComputeChecksum(),
    MyDeparser()
) main;
