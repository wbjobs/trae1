"""
P4 Runtime Controller for DDoS Defense Gateway v2
Handles gRPC communication with BMv2 software switch
Supports 64-bit meters, hierarchical rate limiting (L1+L2), auto-reset
"""

import grpc
import time
import threading
import logging
import hashlib
import struct
from typing import Dict, List, Optional, Tuple
from enum import Enum

import p4runtime_sh.shell as sh
from p4runtime_sh.shell import FwdPipeConfig

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class DropReason(Enum):
    NONE = 0
    ICMP_FLOOD = 1
    UDP_LIMIT = 2
    BLACKLIST = 3
    SYN_FLOOD = 4
    METER_L1 = 5
    METER_L2 = 6


class CounterType(Enum):
    ICMP = 1
    UDP = 2
    TCP_SYN = 3
    BLACKLIST = 4
    TOTAL = 5
    METER_L1 = 6
    METER_L2 = 7


class MeterLevel(Enum):
    L1 = 1
    L2 = 2


class ProtocolType(Enum):
    UDP = 1
    ICMP = 2
    TCP_SYN = 3


METER_SIZE_L1 = 65536
METER_SIZE_L2 = 262144


def compute_l1_index(src_ip: str) -> int:
    """Compute L1 meter index from source IP"""
    h = hashlib.crc32(src_ip.encode()) & 0xFFFFFFFF
    return h % METER_SIZE_L1


def compute_l2_index(src_ip: str, dst_port: int, protocol: int) -> int:
    """Compute L2 meter index from source IP + destination port + protocol"""
    data = src_ip.encode() + struct.pack('!HB', dst_port & 0xFFFF, protocol & 0xFF)
    h = hashlib.crc32(data) & 0xFFFFFFFF
    return h % METER_SIZE_L2


class P4Controller:
    """P4 Runtime Controller for managing BMv2 switch with 64-bit meter support"""

    def __init__(self, grpc_addr: str = "localhost:50051",
                 device_id: int = 0,
                 p4info_path: str = "",
                 bmv2_json_path: str = ""):
        self.grpc_addr = grpc_addr
        self.device_id = device_id
        self.p4info_path = p4info_path
        self.bmv2_json_path = bmv2_json_path

        self._lock = threading.Lock()
        self._connected = False

        self._blacklist: Dict[str, Dict] = {}
        self._udp_rate_limited: Dict[str, Dict] = {}
        self._icmp_blocked: Dict[str, Dict] = {}
        self._syn_flood_blocked: Dict[str, Dict] = {}

        self._meter_l1_config: Dict[int, Dict] = {}
        self._meter_l2_config: Dict[int, Dict] = {}

        self._meter_reset_interval = 10
        self._meter_reset_thread: Optional[threading.Thread] = None
        self._meter_reset_running = False

        self._meter_stats = {
            'l1_udp': {'total': 0, 'overflow_count': 0},
            'l1_icmp': {'total': 0, 'overflow_count': 0},
            'l1_syn': {'total': 0, 'overflow_count': 0},
            'l2_udp': {'total': 0, 'overflow_count': 0},
            'l2_icmp': {'total': 0, 'overflow_count': 0},
            'l2_syn': {'total': 0, 'overflow_count': 0},
        }

    def connect(self) -> bool:
        """Establish gRPC connection to BMv2 switch"""
        try:
            sh.setup(
                device_id=self.device_id,
                grpc_addr=self.grpc_addr,
            )

            if self.p4info_path and self.bmv2_json_path:
                with open(self.p4info_path, 'rb') as p4info_f, \
                     open(self.bmv2_json_path, 'rb') as bmv2_f:
                    p4info_data = p4info_f.read()
                    bmv2_data = bmv2_f.read()

                sh.client.WriteFwdPipe(
                    FwdPipeConfig(
                        cookie=0,
                        p4info=p4info_data,
                        config=bmv2_data
                    )
                )

            self._connected = True
            self._start_meter_reset_loop()
            logger.info(f"Connected to BMv2 at {self.grpc_addr}")
            return True

        except Exception as e:
            logger.error(f"Failed to connect: {e}")
            self._connected = False
            return False

    def disconnect(self):
        """Disconnect from BMv2 switch"""
        try:
            self._stop_meter_reset_loop()
            sh.teardown()
            self._connected = False
            logger.info("Disconnected from BMv2")
        except Exception as e:
            logger.error(f"Error disconnecting: {e}")

    def is_connected(self) -> bool:
        return self._connected

    def _check_connection(self):
        if not self._connected:
            raise ConnectionError("Not connected to BMv2 switch")

    def _start_meter_reset_loop(self):
        """Start background thread for meter reset"""
        self._meter_reset_running = True
        self._meter_reset_thread = threading.Thread(
            target=self._meter_reset_loop, daemon=True
        )
        self._meter_reset_thread.start()
        logger.info("Meter reset loop started")

    def _stop_meter_reset_loop(self):
        """Stop meter reset loop"""
        self._meter_reset_running = False
        if self._meter_reset_thread:
            self._meter_reset_thread.join(timeout=5)
        logger.info("Meter reset loop stopped")

    def _meter_reset_loop(self):
        """Background loop to read and reset meters"""
        while self._meter_reset_running:
            try:
                self._reset_meters()
            except Exception as e:
                logger.error(f"Meter reset error: {e}")
            time.sleep(self._meter_reset_interval)

    def _reset_meters(self):
        """Read and reset all meters"""
        with self._lock:
            try:
                for meter_name in ['l1_udp_meter', 'l1_icmp_meter', 'l1_syn_meter',
                                   'l2_udp_meter', 'l2_icmp_meter', 'l2_syn_meter']:
                    try:
                        self._read_and_reset_counter(meter_name)
                    except Exception as e:
                        logger.debug(f"Reset {meter_name}: {e}")

                logger.debug("Meters reset completed")
            except Exception as e:
                logger.error(f"Failed to reset meters: {e}")

    def _read_and_reset_counter(self, counter_name: str):
        """Read counter values and reset to zero"""
        try:
            counter_entry = sh.CounterEntry(counter_name)
            values = {}
            for entry in counter_entry.read():
                idx = entry.index
                count = entry.data.packet_count if hasattr(entry.data, 'packet_count') else 0
                values[idx] = count

            for idx, count in values.items():
                if count > 0:
                    try:
                        reset_entry = sh.CounterEntry(counter_name)
                        reset_entry.index = idx
                        reset_entry.data.packet_count = 0
                        reset_entry.update()
                    except Exception:
                        pass

            return values
        except Exception as e:
            logger.debug(f"Counter read/reset error: {e}")
            return {}

    def read_counter(self, counter_name: str, index: int = 0) -> Optional[int]:
        """Read counter value from P4"""
        self._check_connection()
        try:
            counter_entry = sh.CounterEntry(counter_name)
            if hasattr(counter_entry, 'index'):
                counter_entry.index = index
            for entry in counter_entry.read():
                return entry.data.packet_count if hasattr(entry.data, 'packet_count') else 0
            return 0
        except Exception as e:
            logger.error(f"Failed to read counter {counter_name}: {e}")
            return None

    def read_direct_counter(self, counter_name: str) -> Optional[int]:
        """Read direct counter value"""
        return self.read_counter(counter_name)

    def read_global_packet_count(self) -> int:
        """Read global packet counter"""
        return self.read_counter('global_packet_counter', 0) or 0

    def read_global_drop_count(self) -> int:
        """Read global drop counter"""
        return self.read_counter('global_drop_counter', 0) or 0

    def add_blacklist_ip(self, ip: str, duration: int = 300) -> bool:
        """Add IP to blacklist table"""
        self._check_connection()
        with self._lock:
            try:
                table_entry = sh.TableEntry('ip_blacklist')(action='drop_and_count')
                table_entry['hdr.ipv4.srcAddr'] = ip
                table_entry['reason'] = str(DropReason.BLACKLIST.value)
                table_entry['counter_type'] = str(CounterType.BLACKLIST.value)
                table_entry.insert()

                self._blacklist[ip] = {
                    'added_at': time.time(),
                    'duration': duration
                }
                logger.info(f"Added {ip} to blacklist for {duration}s")
                return True
            except Exception as e:
                logger.error(f"Failed to blacklist {ip}: {e}")
                return False

    def remove_blacklist_ip(self, ip: str) -> bool:
        """Remove IP from blacklist table"""
        self._check_connection()
        with self._lock:
            try:
                table_entry = sh.TableEntry('ip_blacklist')(action='drop_and_count')
                table_entry['hdr.ipv4.srcAddr'] = ip
                table_entry.delete()

                self._blacklist.pop(ip, None)
                logger.info(f"Removed {ip} from blacklist")
                return True
            except Exception as e:
                logger.error(f"Failed to remove {ip} from blacklist: {e}")
                return False

    def block_icmp_from_ip(self, ip: str) -> bool:
        """Block ICMP packets from specific IP"""
        self._check_connection()
        with self._lock:
            try:
                table_entry = sh.TableEntry('icmp_flood_check')(action='drop_and_count')
                table_entry['hdr.ipv4.srcAddr'] = ip
                table_entry['reason'] = str(DropReason.ICMP_FLOOD.value)
                table_entry['counter_type'] = str(CounterType.ICMP.value)
                table_entry.insert()

                self._icmp_blocked[ip] = {'added_at': time.time()}
                logger.info(f"Blocking ICMP from {ip}")
                return True
            except Exception as e:
                logger.error(f"Failed to block ICMP from {ip}: {e}")
                return False

    def unblock_icmp_from_ip(self, ip: str) -> bool:
        """Unblock ICMP packets from specific IP"""
        self._check_connection()
        with self._lock:
            try:
                table_entry = sh.TableEntry('icmp_flood_check')(action='drop_and_count')
                table_entry['hdr.ipv4.srcAddr'] = ip
                table_entry.delete()

                self._icmp_blocked.pop(ip, None)
                logger.info(f"Unblocking ICMP from {ip}")
                return True
            except Exception as e:
                logger.error(f"Failed to unblock ICMP from {ip}: {e}")
                return False

    def limit_udp_rate(self, ip: str, pps: int = 100) -> bool:
        """Set UDP rate limit for specific IP"""
        self._check_connection()
        with self._lock:
            try:
                table_entry = sh.TableEntry('udp_rate_limit')(action='drop_and_count')
                table_entry['hdr.ipv4.srcAddr'] = ip
                table_entry['reason'] = str(DropReason.UDP_LIMIT.value)
                table_entry['counter_type'] = str(CounterType.UDP.value)
                table_entry.insert()

                self._udp_rate_limited[ip] = {
                    'pps': pps,
                    'added_at': time.time()
                }
                logger.info(f"UDP rate limit set for {ip}: {pps} pps")
                return True
            except Exception as e:
                logger.error(f"Failed to set UDP rate limit for {ip}: {e}")
                return False

    def unlimit_udp_rate(self, ip: str) -> bool:
        """Remove UDP rate limit for specific IP"""
        self._check_connection()
        with self._lock:
            try:
                table_entry = sh.TableEntry('udp_rate_limit')(action='drop_and_count')
                table_entry['hdr.ipv4.srcAddr'] = ip
                table_entry.delete()

                self._udp_rate_limited.pop(ip, None)
                logger.info(f"UDP rate limit removed for {ip}")
                return True
            except Exception as e:
                logger.error(f"Failed to remove UDP rate limit for {ip}: {e}")
                return False

    def block_syn_flood(self, ip: str) -> bool:
        """Block SYN packets from specific IP"""
        self._check_connection()
        with self._lock:
            try:
                table_entry = sh.TableEntry('syn_flood_check')(action='drop_and_count')
                table_entry['hdr.ipv4.srcAddr'] = ip
                table_entry['reason'] = str(DropReason.SYN_FLOOD.value)
                table_entry['counter_type'] = str(CounterType.TCP_SYN.value)
                table_entry.insert()

                self._syn_flood_blocked[ip] = {'added_at': time.time()}
                logger.info(f"Blocking SYN flood from {ip}")
                return True
            except Exception as e:
                logger.error(f"Failed to block SYN flood from {ip}: {e}")
                return False

    def unblock_syn_flood(self, ip: str) -> bool:
        """Unblock SYN packets from specific IP"""
        self._check_connection()
        with self._lock:
            try:
                table_entry = sh.TableEntry('syn_flood_check')(action='drop_and_count')
                table_entry['hdr.ipv4.srcAddr'] = ip
                table_entry.delete()

                self._syn_flood_blocked.pop(ip, None)
                logger.info(f"Unblocking SYN flood from {ip}")
                return True
            except Exception as e:
                logger.error(f"Failed to unblock SYN flood from {ip}: {e}")
                return False

    def add_forwarding_rule(self, dst_ip: str, prefix_len: int,
                            egress_port: int) -> bool:
        """Add IPv4 forwarding rule"""
        self._check_connection()
        with self._lock:
            try:
                table_entry = sh.TableEntry('ipv4_lpm')(action='forward_to_port')
                table_entry['hdr.ipv4.dstAddr'] = f"{dst_ip}/{prefix_len}"
                table_entry['port'] = str(egress_port)
                table_entry.insert()
                logger.info(f"Added forwarding: {dst_ip}/{prefix_len} -> port {egress_port}")
                return True
            except Exception as e:
                logger.error(f"Failed to add forwarding rule: {e}")
                return False

    def configure_l1_meter(self, src_ip: str, protocol: ProtocolType,
                           threshold_pps: int) -> bool:
        """Configure L1 meter threshold for source IP"""
        self._check_connection()
        with self._lock:
            try:
                idx = compute_l1_index(src_ip)
                meter_name = f"l1_{protocol.name.lower()}_meter"

                self._meter_l1_config[idx] = {
                    'src_ip': src_ip,
                    'protocol': protocol.name,
                    'threshold': threshold_pps,
                    'configured_at': time.time()
                }

                table_entry = sh.TableEntry('l1_meter_config')(action='drop_and_count')
                table_entry['meta.meter_index_l1'] = str(idx)
                table_entry['reason'] = str(DropReason.METER_L1.value)
                table_entry['counter_type'] = str(CounterType.METER_L1.value)
                table_entry.insert()

                logger.info(f"L1 meter configured: IP={src_ip}, protocol={protocol.name}, "
                          f"threshold={threshold_pps} pps, index={idx}")
                return True
            except Exception as e:
                logger.error(f"Failed to configure L1 meter: {e}")
                return False

    def configure_l2_meter(self, src_ip: str, dst_port: int,
                           protocol: ProtocolType, threshold_pps: int) -> bool:
        """Configure L2 meter threshold for source IP + destination port"""
        self._check_connection()
        with self._lock:
            try:
                protocol_num = protocol.value
                idx = compute_l2_index(src_ip, dst_port, protocol_num)
                meter_name = f"l2_{protocol.name.lower()}_meter"

                self._meter_l2_config[idx] = {
                    'src_ip': src_ip,
                    'dst_port': dst_port,
                    'protocol': protocol.name,
                    'threshold': threshold_pps,
                    'configured_at': time.time()
                }

                table_entry = sh.TableEntry('l2_meter_config')(action='drop_and_count')
                table_entry['meta.meter_index_l2'] = str(idx)
                table_entry['reason'] = str(DropReason.METER_L2.value)
                table_entry['counter_type'] = str(CounterType.METER_L2.value)
                table_entry.insert()

                logger.info(f"L2 meter configured: IP={src_ip}, port={dst_port}, "
                          f"protocol={protocol.name}, threshold={threshold_pps} pps, index={idx}")
                return True
            except Exception as e:
                logger.error(f"Failed to configure L2 meter: {e}")
                return False

    def remove_l1_meter(self, src_ip: str, protocol: ProtocolType) -> bool:
        """Remove L1 meter configuration"""
        self._check_connection()
        with self._lock:
            try:
                idx = compute_l1_index(src_ip)

                table_entry = sh.TableEntry('l1_meter_config')(action='drop_and_count')
                table_entry['meta.meter_index_l1'] = str(idx)
                table_entry.delete()

                self._meter_l1_config.pop(idx, None)
                logger.info(f"L1 meter removed: IP={src_ip}, protocol={protocol.name}")
                return True
            except Exception as e:
                logger.error(f"Failed to remove L1 meter: {e}")
                return False

    def remove_l2_meter(self, src_ip: str, dst_port: int,
                        protocol: ProtocolType) -> bool:
        """Remove L2 meter configuration"""
        self._check_connection()
        with self._lock:
            try:
                protocol_num = protocol.value
                idx = compute_l2_index(src_ip, dst_port, protocol_num)

                table_entry = sh.TableEntry('l2_meter_config')(action='drop_and_count')
                table_entry['meta.meter_index_l2'] = str(idx)
                table_entry.delete()

                self._meter_l2_config.pop(idx, None)
                logger.info(f"L2 meter removed: IP={src_ip}, port={dst_port}, "
                          f"protocol={protocol.name}")
                return True
            except Exception as e:
                logger.error(f"Failed to remove L2 meter: {e}")
                return False

    def get_l1_meter_stats(self) -> List[Dict]:
        """Get L1 meter statistics"""
        return list(self._meter_l1_config.values())

    def get_l2_meter_stats(self) -> List[Dict]:
        """Get L2 meter statistics"""
        return list(self._meter_l2_config.values())

    def get_blacklist_ips(self) -> List[Dict]:
        """Get all blacklisted IPs with info"""
        return [
            {'ip': ip, **info}
            for ip, info in self._blacklist.items()
        ]

    def get_udp_rate_limited_ips(self) -> List[Dict]:
        """Get all UDP rate-limited IPs"""
        return [
            {'ip': ip, **info}
            for ip, info in self._udp_rate_limited.items()
        ]

    def get_icmp_blocked_ips(self) -> List[Dict]:
        """Get all ICMP blocked IPs"""
        return [
            {'ip': ip, **info}
            for ip, info in self._icmp_blocked.items()
        ]

    def get_syn_blocked_ips(self) -> List[Dict]:
        """Get all SYN blocked IPs"""
        return [
            {'ip': ip, **info}
            for ip, info in self._syn_flood_blocked.items()
        ]

    def get_meter_stats(self) -> Dict:
        """Get comprehensive meter statistics"""
        return {
            'l1_configured': len(self._meter_l1_config),
            'l2_configured': len(self._meter_l2_config),
            'l1_entries': self.get_l1_meter_stats(),
            'l2_entries': self.get_l2_meter_stats(),
            'reset_interval': self._meter_reset_interval,
            'meter_stats': dict(self._meter_stats),
        }

    def cleanup_expired_entries(self):
        """Remove expired entries based on duration"""
        current_time = time.time()
        expired_ips = []

        for ip, info in self._blacklist.items():
            if current_time - info['added_at'] > info.get('duration', 300):
                expired_ips.append(ip)

        for ip in expired_ips:
            self.remove_blacklist_ip(ip)

        logger.info(f"Cleaned up {len(expired_ips)} expired entries")
        return len(expired_ips)
