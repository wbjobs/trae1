"""
Auto-Block Module for DDoS Defense Gateway v2
Monitors traffic and automatically blocks attack sources when thresholds are exceeded
Supports hierarchical rate limiting (L1: source IP, L2: source IP + port)
"""

import time
import threading
import logging
from collections import defaultdict
from typing import Dict, List, Optional, Callable

from .p4_controller import ProtocolType

logger = logging.getLogger(__name__)


class TrafficMonitor:
    """Monitors traffic patterns and detects attacks with hierarchical support"""

    def __init__(self):
        self._lock = threading.Lock()

        self._udp_stats: Dict[str, Dict] = defaultdict(lambda: {
            'packets': 0,
            'timestamps': [],
            'blocked': False,
            'l1_blocked': False,
            'l2_blocked': False,
            'l2_ports': defaultdict(int)
        })

        self._icmp_stats: Dict[str, Dict] = defaultdict(lambda: {
            'packets': 0,
            'timestamps': [],
            'blocked': False,
            'l1_blocked': False,
            'l2_blocked': False,
            'l2_ports': defaultdict(int)
        })

        self._syn_stats: Dict[str, Dict] = defaultdict(lambda: {
            'packets': 0,
            'timestamps': [],
            'blocked': False,
            'l1_blocked': False,
            'l2_blocked': False,
            'l2_ports': defaultdict(int)
        })

        self._blacklisted_ips: Dict[str, Dict] = {}

        self._thresholds = {
            'udp_pps': 100,
            'icmp_pps': 50,
            'syn_pps': 200,
            'l2_udp_pps': 50,
            'l2_icmp_pps': 25,
            'l2_syn_pps': 100,
            'window_seconds': 1
        }

        self._block_duration = 300
        self._l1_block_duration = 180
        self._l2_block_duration = 120

        self._on_block_callbacks: List[Callable] = []
        self._on_unblock_callbacks: List[Callable] = []
        self._on_l1_block_callbacks: List[Callable] = []
        self._on_l2_block_callbacks: List[Callable] = []

    def configure_thresholds(self, udp_pps: int = 100, icmp_pps: int = 50,
                              syn_pps: int = 200, window_seconds: int = 1,
                              block_duration: int = 300,
                              l2_udp_pps: int = 50, l2_icmp_pps: int = 25,
                              l2_syn_pps: int = 100,
                              l1_block_duration: int = 180,
                              l2_block_duration: int = 120):
        """Configure detection thresholds"""
        with self._lock:
            self._thresholds['udp_pps'] = udp_pps
            self._thresholds['icmp_pps'] = icmp_pps
            self._thresholds['syn_pps'] = syn_pps
            self._thresholds['l2_udp_pps'] = l2_udp_pps
            self._thresholds['l2_icmp_pps'] = l2_icmp_pps
            self._thresholds['l2_syn_pps'] = l2_syn_pps
            self._thresholds['window_seconds'] = window_seconds
            self._block_duration = block_duration
            self._l1_block_duration = l1_block_duration
            self._l2_block_duration = l2_block_duration

    def register_on_block(self, callback: Callable):
        """Register callback for when IP is blocked"""
        self._on_block_callbacks.append(callback)

    def register_on_unblock(self, callback: Callable):
        """Register callback for when IP is unblocked"""
        self._on_unblock_callbacks.append(callback)

    def register_on_l1_block(self, callback: Callable):
        """Register callback for L1 meter block"""
        self._on_l1_block_callbacks.append(callback)

    def register_on_l2_block(self, callback: Callable):
        """Register callback for L2 meter block"""
        self._on_l2_block_callbacks.append(callback)

    def record_udp_packet(self, src_ip: str, dst_port: int = 0) -> Optional[Dict]:
        """Record a UDP packet and check thresholds"""
        return self._check_and_update(
            src_ip, dst_port, 'udp', self._udp_stats,
            self._thresholds['udp_pps'], self._thresholds['l2_udp_pps']
        )

    def record_icmp_packet(self, src_ip: str, dst_port: int = 0) -> Optional[Dict]:
        """Record an ICMP packet and check thresholds"""
        return self._check_and_update(
            src_ip, dst_port, 'icmp', self._icmp_stats,
            self._thresholds['icmp_pps'], self._thresholds['l2_icmp_pps']
        )

    def record_syn_packet(self, src_ip: str, dst_port: int = 0) -> Optional[Dict]:
        """Record a SYN packet and check thresholds"""
        return self._check_and_update(
            src_ip, dst_port, 'syn', self._syn_stats,
            self._thresholds['syn_pps'], self._thresholds['l2_syn_pps']
        )

    def _check_and_update(self, src_ip: str, dst_port: int, traffic_type: str,
                          stats_dict: Dict, l1_threshold: int,
                          l2_threshold: int) -> Optional[Dict]:
        """Check if traffic exceeds thresholds and update stats"""
        with self._lock:
            now = time.time()
            window = self._thresholds['window_seconds']

            stats = stats_dict[src_ip]
            stats['timestamps'] = [
                ts for ts in stats['timestamps']
                if now - ts < window
            ]
            stats['timestamps'].append(now)
            stats['packets'] = len(stats['timestamps'])

            if dst_port != 0:
                stats['l2_ports'][dst_port] = stats['l2_ports'].get(dst_port, 0) + 1

            result = {'l1_blocked': False, 'l2_blocked': False, 'fully_blocked': False}

            if not stats['l1_blocked'] and stats['packets'] > l1_threshold:
                stats['l1_blocked'] = True
                result['l1_blocked'] = True
                for cb in self._on_l1_block_callbacks:
                    try:
                        cb(src_ip, traffic_type, stats['packets'])
                    except Exception as e:
                        logger.error(f"L1 callback error: {e}")

            if dst_port != 0 and not stats['l2_blocked']:
                port_packets = stats['l2_ports'].get(dst_port, 0)
                if port_packets > l2_threshold:
                    stats['l2_blocked'] = True
                    result['l2_blocked'] = True
                    for cb in self._on_l2_block_callbacks:
                        try:
                            cb(src_ip, dst_port, traffic_type, port_packets)
                        except Exception as e:
                            logger.error(f"L2 callback error: {e}")

            if not stats['blocked'] and (result['l1_blocked'] or result['l2_blocked']):
                stats['blocked'] = True
                result['fully_blocked'] = True
                self._blacklisted_ips[src_ip] = {
                    'type': traffic_type,
                    'packets': stats['packets'],
                    'blocked_at': now,
                    'duration': self._block_duration,
                    'l1_blocked': result['l1_blocked'],
                    'l2_blocked': result['l2_blocked'],
                    'l2_ports': dict(stats['l2_ports'])
                }
                for cb in self._on_block_callbacks:
                    try:
                        cb(src_ip, traffic_type, stats['packets'])
                    except Exception as e:
                        logger.error(f"Block callback error: {e}")

            return result

    def is_blacklisted(self, src_ip: str) -> bool:
        """Check if IP is currently blacklisted"""
        with self._lock:
            if src_ip not in self._blacklisted_ips:
                return False

            info = self._blacklisted_ips[src_ip]
            if time.time() - info['blocked_at'] > info['duration']:
                self._unblock_ip(src_ip)
                return False

            return True

    def _unblock_ip(self, src_ip: str):
        """Remove IP from blacklist"""
        if src_ip in self._blacklisted_ips:
            info = self._blacklisted_ips.pop(src_ip)

            traffic_type = info['type']
            if traffic_type == 'udp' and src_ip in self._udp_stats:
                self._udp_stats[src_ip]['blocked'] = False
                self._udp_stats[src_ip]['l1_blocked'] = False
                self._udp_stats[src_ip]['l2_blocked'] = False
            elif traffic_type == 'icmp' and src_ip in self._icmp_stats:
                self._icmp_stats[src_ip]['blocked'] = False
                self._icmp_stats[src_ip]['l1_blocked'] = False
                self._icmp_stats[src_ip]['l2_blocked'] = False
            elif traffic_type == 'syn' and src_ip in self._syn_stats:
                self._syn_stats[src_ip]['blocked'] = False
                self._syn_stats[src_ip]['l1_blocked'] = False
                self._syn_stats[src_ip]['l2_blocked'] = False

            for cb in self._on_unblock_callbacks:
                try:
                    cb(src_ip, traffic_type)
                except Exception as e:
                    logger.error(f"Unblock callback error: {e}")

    def cleanup_expired(self) -> int:
        """Remove expired entries, return number cleaned up"""
        with self._lock:
            now = time.time()
            expired = [
                ip for ip, info in self._blacklisted_ips.items()
                if now - info['blocked_at'] > info['duration']
            ]
            for ip in expired:
                self._unblock_ip(ip)
            return len(expired)

    def get_blacklisted_ips(self) -> List[Dict]:
        """Get all blacklisted IPs with details"""
        with self._lock:
            now = time.time()
            return [
                {
                    'ip': ip,
                    'type': info['type'],
                    'packets': info['packets'],
                    'blocked_at': info['blocked_at'],
                    'remaining': info['duration'] - (now - info['blocked_at']),
                    'l1_blocked': info.get('l1_blocked', False),
                    'l2_blocked': info.get('l2_blocked', False),
                    'l2_ports': info.get('l2_ports', {})
                }
                for ip, info in self._blacklisted_ips.items()
            ]

    def get_stats(self) -> Dict:
        """Get traffic statistics"""
        with self._lock:
            return {
                'blacklisted_count': len(self._blacklisted_ips),
                'udp_monitored': len(self._udp_stats),
                'icmp_monitored': len(self._icmp_stats),
                'syn_monitored': len(self._syn_stats),
                'l1_blocked_count': sum(
                    1 for stats in [*self._udp_stats.values(),
                                    *self._icmp_stats.values(),
                                    *self._syn_stats.values()]
                    if stats['l1_blocked']
                ),
                'l2_blocked_count': sum(
                    1 for stats in [*self._udp_stats.values(),
                                    *self._icmp_stats.values(),
                                    *self._syn_stats.values()]
                    if stats['l2_blocked']
                ),
                'thresholds': dict(self._thresholds)
            }


class AutoBlocker:
    """Coordinates automatic blocking of attack sources with hierarchical support"""

    def __init__(self, p4_controller, metrics_collector):
        self.p4_controller = p4_controller
        self.metrics = metrics_collector
        self.traffic_monitor = TrafficMonitor()

        self._lock = threading.Lock()
        self._running = False
        self._cleanup_thread: Optional[threading.Thread] = None
        self._counter_thread: Optional[threading.Thread] = None

        self.traffic_monitor.register_on_block(self._on_block)
        self.traffic_monitor.register_on_unblock(self._on_unblock)
        self.traffic_monitor.register_on_l1_block(self._on_l1_block)
        self.traffic_monitor.register_on_l2_block(self._on_l2_block)

    def start(self):
        """Start the auto blocker"""
        self._running = True
        self._cleanup_thread = threading.Thread(
            target=self._cleanup_loop, daemon=True
        )
        self._cleanup_thread.start()

        self._counter_thread = threading.Thread(
            target=self._counter_update_loop, daemon=True
        )
        self._counter_thread.start()

        logger.info("AutoBlocker started with hierarchical rate limiting")

    def stop(self):
        """Stop the auto blocker"""
        self._running = False
        if self._cleanup_thread:
            self._cleanup_thread.join(timeout=5)
        if self._counter_thread:
            self._counter_thread.join(timeout=5)
        logger.info("AutoBlocker stopped")

    def _cleanup_loop(self):
        """Background cleanup loop for expired entries"""
        while self._running:
            try:
                cleaned = self.traffic_monitor.cleanup_expired()
                if cleaned > 0:
                    logger.info(f"Cleaned up {cleaned} expired entries")
            except Exception as e:
                logger.error(f"Cleanup error: {e}")
            time.sleep(10)

    def _counter_update_loop(self):
        """Background loop to update global counters"""
        while self._running:
            try:
                packet_count = self.p4_controller.read_global_packet_count()
                drop_count = self.p4_controller.read_global_drop_count()
                self.metrics.update_global_counters(packet_count, drop_count)
            except Exception as e:
                logger.debug(f"Counter update error: {e}")
            time.sleep(5)

    def _on_block(self, src_ip: str, traffic_type: str, packets: int):
        """Handle IP block event"""
        logger.warning(f"Auto-blocking {src_ip} ({traffic_type}): {packets} pps")

        try:
            if traffic_type == 'udp':
                self.p4_controller.limit_udp_rate(src_ip)
            elif traffic_type == 'icmp':
                self.p4_controller.block_icmp_from_ip(src_ip)
            elif traffic_type == 'syn':
                self.p4_controller.block_syn_flood(src_ip)

            self.p4_controller.add_blacklist_ip(src_ip)

            self.metrics.record_attack_detected(traffic_type)
            self.metrics.record_defense_action(f"block_{traffic_type}")
            self.metrics.auto_blocked_ips_total.inc()

            self._update_metrics()

        except Exception as e:
            logger.error(f"Failed to block {src_ip}: {e}")

    def _on_l1_block(self, src_ip: str, traffic_type: str, packets: int):
        """Handle L1 meter block event"""
        logger.warning(f"L1 meter block: {src_ip} ({traffic_type}): {packets} pps")

        try:
            protocol_map = {
                'udp': ProtocolType.UDP,
                'icmp': ProtocolType.ICMP,
                'syn': ProtocolType.TCP_SYN
            }
            protocol = protocol_map.get(traffic_type)
            if protocol:
                l1_threshold = getattr(self.traffic_monitor._thresholds,
                                       f'{traffic_type}_pps', 100)
                self.p4_controller.configure_l1_meter(src_ip, protocol, l1_threshold)

            self.metrics.record_defense_action(f'l1_block_{traffic_type}')
            self._update_metrics()

        except Exception as e:
            logger.error(f"Failed to configure L1 meter for {src_ip}: {e}")

    def _on_l2_block(self, src_ip: str, dst_port: int, traffic_type: str,
                     packets: int):
        """Handle L2 meter block event"""
        logger.warning(f"L2 meter block: {src_ip}:{dst_port} ({traffic_type}): "
                      f"{packets} pps")

        try:
            protocol_map = {
                'udp': ProtocolType.UDP,
                'icmp': ProtocolType.ICMP,
                'syn': ProtocolType.TCP_SYN
            }
            protocol = protocol_map.get(traffic_type)
            if protocol:
                l2_threshold = getattr(self.traffic_monitor._thresholds,
                                       f'l2_{traffic_type}_pps', 50)
                self.p4_controller.configure_l2_meter(
                    src_ip, dst_port, protocol, l2_threshold
                )

            self.metrics.record_defense_action(f'l2_block_{traffic_type}')
            self._update_metrics()

        except Exception as e:
            logger.error(f"Failed to configure L2 meter for {src_ip}:{dst_port}: {e}")

    def _on_unblock(self, src_ip: str, traffic_type: str):
        """Handle IP unblock event"""
        logger.info(f"Auto-unblocking {src_ip} ({traffic_type})")

        try:
            if traffic_type == 'udp':
                self.p4_controller.unlimit_udp_rate(src_ip)
            elif traffic_type == 'icmp':
                self.p4_controller.unblock_icmp_from_ip(src_ip)
            elif traffic_type == 'syn':
                self.p4_controller.unblock_syn_flood(src_ip)

            self.p4_controller.remove_blacklist_ip(src_ip)

            self._update_metrics()

        except Exception as e:
            logger.error(f"Failed to unblock {src_ip}: {e}")

    def _update_metrics(self):
        """Update metrics with current state"""
        self.metrics.update_blacklist_count(
            len(self.p4_controller.get_blacklist_ips())
        )
        self.metrics.update_udp_rate_limited_count(
            len(self.p4_controller.get_udp_rate_limited_ips())
        )
        self.metrics.update_icmp_blocked_count(
            len(self.p4_controller.get_icmp_blocked_ips())
        )
        self.metrics.update_syn_blocked_count(
            len(self.p4_controller.get_syn_blocked_ips())
        )
        self.metrics.update_l1_meter_count(
            len(self.p4_controller.get_l1_meter_stats())
        )
        self.metrics.update_l2_meter_count(
            len(self.p4_controller.get_l2_meter_stats())
        )

    def configure(self, udp_pps: int = 100, icmp_pps: int = 50,
                  syn_pps: int = 200, window_seconds: int = 1,
                  block_duration: int = 300,
                  l2_udp_pps: int = 50, l2_icmp_pps: int = 25,
                  l2_syn_pps: int = 100,
                  l1_block_duration: int = 180,
                  l2_block_duration: int = 120):
        """Configure detection parameters"""
        self.traffic_monitor.configure_thresholds(
            udp_pps=udp_pps,
            icmp_pps=icmp_pps,
            syn_pps=syn_pps,
            window_seconds=window_seconds,
            block_duration=block_duration,
            l2_udp_pps=l2_udp_pps,
            l2_icmp_pps=l2_icmp_pps,
            l2_syn_pps=l2_syn_pps,
            l1_block_duration=l1_block_duration,
            l2_block_duration=l2_block_duration
        )

    def record_packet(self, src_ip: str, protocol: str, dst_port: int = 0):
        """Record a packet for monitoring"""
        if protocol == 'udp':
            return self.traffic_monitor.record_udp_packet(src_ip, dst_port)
        elif protocol == 'icmp':
            return self.traffic_monitor.record_icmp_packet(src_ip, dst_port)
        elif protocol == 'tcp_syn':
            return self.traffic_monitor.record_syn_packet(src_ip, dst_port)
        return None

    def get_status(self) -> Dict:
        """Get current auto blocker status"""
        return {
            'running': self._running,
            'traffic_stats': self.traffic_monitor.get_stats(),
            'blacklisted_ips': self.traffic_monitor.get_blacklisted_ips(),
            'meter_stats': self.p4_controller.get_meter_stats()
        }
