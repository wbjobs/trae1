"""
Traffic Feature Collector for DDoS Defense Gateway
Collects normal traffic characteristics: packet rate, connections, protocol distribution
"""

import time
import threading
import logging
from collections import defaultdict, deque
from typing import Dict, List, Optional, Tuple
from enum import Enum

logger = logging.getLogger(__name__)


class ProtocolType(Enum):
    TCP = 6
    UDP = 17
    ICMP = 1


class TrafficFeature:
    """Represents a single traffic feature measurement"""
    
    def __init__(self, name: str, value: float, timestamp: float = None):
        self.name = name
        self.value = value
        self.timestamp = timestamp or time.time()


class TrafficCollector:
    """Collects and stores traffic features over time"""

    def __init__(self, window_size: int = 300, sample_interval: int = 60):
        """
        Initialize traffic collector
        
        Args:
            window_size: Number of samples to keep in history
            sample_interval: Interval between samples in seconds
        """
        self._window_size = window_size
        self._sample_interval = sample_interval

        self._lock = threading.Lock()

        self._packet_rate_history: deque = deque(maxlen=window_size)
        self._connection_rate_history: deque = deque(maxlen=window_size)
        self._protocol_distribution_history: deque = deque(maxlen=window_size)

        self._current_window_start = time.time()
        self._current_packet_count = 0
        self._current_connection_count = 0
        self._current_protocol_counts: Dict[int, int] = defaultdict(int)

        self._per_ip_stats: Dict[str, Dict] = defaultdict(lambda: {
            'packet_count': 0,
            'connection_count': 0,
            'syn_count': 0,
            'udp_count': 0,
            'icmp_count': 0,
            'last_seen': 0
        })

        self._lock = threading.Lock()
        self._collection_thread: Optional[threading.Thread] = None
        self._running = False

    def start(self):
        """Start the background collection thread"""
        self._running = True
        self._collection_thread = threading.Thread(
            target=self._collection_loop, daemon=True
        )
        self._collection_thread.start()
        logger.info("Traffic collector started")

    def stop(self):
        """Stop the background collection thread"""
        self._running = False
        if self._collection_thread:
            self._collection_thread.join(timeout=5)
        logger.info("Traffic collector stopped")

    def _collection_loop(self):
        """Background loop to periodically collect traffic features"""
        while self._running:
            try:
                time.sleep(self._sample_interval)
                self._collect_and_reset()
            except Exception as e:
                logger.error(f"Collection error: {e}")

    def _collect_and_reset(self):
        """Collect current window stats and reset counters"""
        with self._lock:
            now = time.time()
            elapsed = now - self._current_window_start

            if elapsed <= 0:
                elapsed = 1

            packet_rate = self._current_packet_count / elapsed
            connection_rate = self._current_connection_count / elapsed

            total_packets = sum(self._current_protocol_counts.values())
            if total_packets > 0:
                protocol_dist = {
                    proto: count / total_packets
                    for proto, count in self._current_protocol_counts.items()
                }
            else:
                protocol_dist = {}

            feature = {
                'timestamp': now,
                'packet_rate': packet_rate,
                'connection_rate': connection_rate,
                'protocol_distribution': protocol_dist,
                'total_packets': self._current_packet_count,
                'total_connections': self._current_connection_count
            }

            self._packet_rate_history.append(packet_rate)
            self._connection_rate_history.append(connection_rate)
            self._protocol_distribution_history.append(protocol_dist)

            self._current_window_start = now
            self._current_packet_count = 0
            self._current_connection_count = 0
            self._current_protocol_counts.clear()

            logger.debug(f"Collected: rate={packet_rate:.2f}pps, "
                        f"conns={connection_rate:.2f}/s")

    def record_packet(self, src_ip: str, protocol: int, flags: Dict = None):
        """Record a packet for traffic statistics"""
        with self._lock:
            self._current_packet_count += 1
            self._current_protocol_counts[protocol] += 1

            if src_ip in self._per_ip_stats:
                stats = self._per_ip_stats[src_ip]
                stats['packet_count'] += 1
                stats['last_seen'] = time.time()

                if protocol == ProtocolType.TCP.value:
                    if flags and flags.get('syn') and not flags.get('ack'):
                        stats['syn_count'] += 1
                        self._current_connection_count += 1
                elif protocol == ProtocolType.UDP.value:
                    stats['udp_count'] += 1
                elif protocol == ProtocolType.ICMP.value:
                    stats['icmp_count'] += 1

    def get_current_stats(self) -> Dict:
        """Get current traffic statistics"""
        with self._lock:
            now = time.time()
            elapsed = max(now - self._current_window_start, 1)

            return {
                'current_packet_rate': self._current_packet_count / elapsed,
                'current_connection_rate': self._current_connection_count / elapsed,
                'current_protocol_counts': dict(self._current_protocol_counts),
                'monitored_ips': len(self._per_ip_stats),
                'history_size': len(self._packet_rate_history)
            }

    def get_packet_rate_history(self) -> List[float]:
        """Get packet rate history"""
        with self._lock:
            return list(self._packet_rate_history)

    def get_connection_rate_history(self) -> List[float]:
        """Get connection rate history"""
        with self._lock:
            return list(self._connection_rate_history)

    def get_protocol_distribution_history(self) -> List[Dict]:
        """Get protocol distribution history"""
        with self._lock:
            return list(self._protocol_distribution_history)

    def get_all_features(self) -> Dict[str, List]:
        """Get all collected features"""
        with self._lock:
            return {
                'packet_rates': list(self._packet_rate_history),
                'connection_rates': list(self._connection_rate_history),
                'protocol_distributions': list(self._protocol_distribution_history),
                'sample_count': len(self._packet_rate_history)
            }

    def get_per_ip_stats(self, ip: str) -> Optional[Dict]:
        """Get statistics for a specific IP"""
        with self._lock:
            if ip in self._per_ip_stats:
                return dict(self._per_ip_stats[ip])
            return None

    def get_top_talkers(self, limit: int = 10) -> List[Dict]:
        """Get top talkers by packet count"""
        with self._lock:
            sorted_ips = sorted(
                self._per_ip_stats.items(),
                key=lambda x: x[1]['packet_count'],
                reverse=True
            )[:limit]

            return [
                {
                    'ip': ip,
                    'packets': stats['packet_count'],
                    'connections': stats['connection_count'],
                    'syn_packets': stats['syn_count'],
                    'udp_packets': stats['udp_count'],
                    'icmp_packets': stats['icmp_count'],
                    'last_seen': stats['last_seen']
                }
                for ip, stats in sorted_ips
            ]

    def get_statistics(self) -> Dict:
        """Get statistical summary of collected features"""
        with self._lock:
            result = {}

            for name, history in [
                ('packet_rate', self._packet_rate_history),
                ('connection_rate', self._connection_rate_history)
            ]:
                if history:
                    values = list(history)
                    result[name] = {
                        'mean': sum(values) / len(values),
                        'min': min(values),
                        'max': max(values),
                        'std': (sum((v - sum(values)/len(values))**2 for v in values) / len(values)) ** 0.5,
                        'count': len(values)
                    }

            return result

    def cleanup_old_ips(self, timeout: int = 3600):
        """Remove IPs not seen for timeout seconds"""
        with self._lock:
            now = time.time()
            old_ips = [
                ip for ip, stats in self._per_ip_stats.items()
                if now - stats['last_seen'] > timeout
            ]
            for ip in old_ips:
                del self._per_ip_stats[ip]

            logger.info(f"Cleaned up {len(old_ips)} old IP entries")
