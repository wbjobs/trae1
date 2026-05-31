"""
Prometheus Metrics Collector for DDoS Defense Gateway v3
Exposes metrics about dropped packets, hierarchical meters, and ML adaptive thresholds
"""

import time
import threading
import logging
from typing import Dict, Optional

from prometheus_client import Counter, Gauge, Histogram, generate_latest

logger = logging.getLogger(__name__)


class MetricsCollector:
    """Collects and exposes Prometheus metrics for DDoS defense with ML support"""

    def __init__(self):
        self.packets_dropped_total = Counter(
            'ddos_packets_dropped_total',
            'Total number of packets dropped by DDoS defense',
            ['drop_reason', 'source_ip']
        )

        self.packets_dropped_by_type = Counter(
            'ddos_packets_dropped_by_type',
            'Packets dropped categorized by attack type',
            ['attack_type']
        )

        self.blacklist_entries_count = Gauge(
            'ddos_blacklist_entries',
            'Current number of IPs in blacklist'
        )

        self.udp_rate_limited_count = Gauge(
            'ddos_udp_rate_limited_ips',
            'Current number of IPs with UDP rate limits'
        )

        self.icmp_blocked_count = Gauge(
            'ddos_icmp_blocked_ips',
            'Current number of IPs blocked for ICMP flood'
        )

        self.syn_blocked_count = Gauge(
            'ddos_syn_blocked_ips',
            'Current number of IPs blocked for SYN flood'
        )

        # ML Adaptive Threshold Metrics
        self.ml_model_trained = Gauge(
            'ddos_ml_model_trained',
            'ML model training status (1=trained, 0=not trained)'
        )

        self.ml_mode = Gauge(
            'ddos_ml_mode',
            'ML operation mode (1=auto, 2=manual)'
        )

        self.ml_threshold_adjustments_total = Counter(
            'ddos_ml_threshold_adjustments_total',
            'Total number of ML-driven threshold adjustments',
            ['threshold_type']
        )

        self.ml_current_threshold = Gauge(
            'ddos_ml_current_threshold',
            'Current ML-computed threshold',
            ['threshold_type']
        )

        self.ml_baseline_mean = Gauge(
            'ddos_ml_baseline_mean',
            'ML model baseline mean for each metric',
            ['threshold_type']
        )

        self.ml_baseline_std = Gauge(
            'ddos_ml_baseline_std',
            'ML model baseline std for each metric',
            ['threshold_type']
        )

        self.ml_pending_approvals = Gauge(
            'ddos_ml_pending_approvals',
            'Number of pending threshold approvals (manual mode)'
        )

        self.ml_training_samples = Gauge(
            'ddos_ml_training_samples',
            'Number of training samples collected'
        )

        self.l1_meter_configured_count = Gauge(
            'ddos_l1_meter_configured',
            'Current number of L1 (source IP) meters configured'
        )

        self.l2_meter_configured_count = Gauge(
            'ddos_l2_meter_configured',
            'Current number of L2 (source IP + port) meters configured'
        )

        self.l1_meter_drop_total = Counter(
            'ddos_l1_meter_drops_total',
            'Total packets dropped by L1 meters',
            ['protocol']
        )

        self.l2_meter_drop_total = Counter(
            'ddos_l2_meter_drops_total',
            'Total packets dropped by L2 meters',
            ['protocol']
        )

        self.meter_overflow_total = Counter(
            'ddos_meter_overflow_total',
            'Total meter overflow events',
            ['level', 'protocol']
        )

        self.meter_reset_count = Counter(
            'ddos_meter_resets_total',
            'Total number of meter reset cycles'
        )

        self.attack_detected_total = Counter(
            'ddos_attacks_detected_total',
            'Total number of attacks detected',
            ['attack_type']
        )

        self.defense_actions_total = Counter(
            'ddos_defense_actions_total',
            'Total number of defense actions taken',
            ['action_type']
        )

        self.auto_blocked_ips_total = Counter(
            'ddos_auto_blocked_ips_total',
            'Total number of IPs automatically blocked'
        )

        self.packets_processed_total = Counter(
            'ddos_packets_processed_total',
            'Total number of packets processed'
        )

        self.global_packet_count = Gauge(
            'ddos_global_packet_count',
            'Global packet counter from P4 switch'
        )

        self.global_drop_count = Gauge(
            'ddos_global_drop_count',
            'Global drop counter from P4 switch'
        )

        self.cpu_usage = Gauge(
            'ddos_controller_cpu_usage',
            'CPU usage of the controller'
        )

        self.memory_usage = Gauge(
            'ddos_controller_memory_usage',
            'Memory usage of the controller'
        )

        self.uptime_seconds = Gauge(
            'ddos_controller_uptime_seconds',
            'Controller uptime in seconds'
        )

        self._start_time = time.time()
        self._lock = threading.Lock()

        self._drop_stats: Dict[str, Dict] = {
            'icmp_flood': {'total': 0, 'by_ip': {}},
            'udp_limit': {'total': 0, 'by_ip': {}},
            'blacklist': {'total': 0, 'by_ip': {}},
            'syn_flood': {'total': 0, 'by_ip': {}},
            'meter_l1': {'total': 0, 'by_ip': {}},
            'meter_l2': {'total': 0, 'by_ip': {}},
        }

        self._meter_stats = {
            'l1': {'udp': 0, 'icmp': 0, 'syn': 0},
            'l2': {'udp': 0, 'icmp': 0, 'syn': 0},
        }

    def record_drop(self, drop_reason: str, source_ip: str, count: int = 1):
        """Record a packet drop event"""
        with self._lock:
            if drop_reason in self._drop_stats:
                self._drop_stats[drop_reason]['total'] += count
                if source_ip not in self._drop_stats[drop_reason]['by_ip']:
                    self._drop_stats[drop_reason]['by_ip'][source_ip] = 0
                self._drop_stats[drop_reason]['by_ip'][source_ip] += count

            self.packets_dropped_total.labels(
                drop_reason=drop_reason,
                source_ip=source_ip
            ).inc(count)

            self.packets_dropped_by_type.labels(
                attack_type=drop_reason
            ).inc(count)

    def record_meter_drop(self, level: str, protocol: str, count: int = 1):
        """Record a meter-based drop event"""
        with self._lock:
            if level in self._meter_stats and protocol in self._meter_stats[level]:
                self._meter_stats[level][protocol] += count

        if level == 'l1':
            self.l1_meter_drop_total.labels(protocol=protocol).inc(count)
        elif level == 'l2':
            self.l2_meter_drop_total.labels(protocol=protocol).inc(count)

    def record_meter_overflow(self, level: str, protocol: str):
        """Record a meter overflow event"""
        self.meter_overflow_total.labels(level=level, protocol=protocol).inc()

    def record_meter_reset(self):
        """Record a meter reset cycle"""
        self.meter_reset_count.inc()

    def record_attack_detected(self, attack_type: str):
        """Record an attack detection event"""
        self.attack_detected_total.labels(attack_type=attack_type).inc()

    def record_defense_action(self, action_type: str):
        """Record a defense action taken"""
        self.defense_actions_total.labels(action_type=action_type).inc()

    def update_blacklist_count(self, count: int):
        """Update blacklist entries count"""
        self.blacklist_entries_count.set(count)

    def update_udp_rate_limited_count(self, count: int):
        """Update UDP rate limited IPs count"""
        self.udp_rate_limited_count.set(count)

    def update_icmp_blocked_count(self, count: int):
        """Update ICMP blocked IPs count"""
        self.icmp_blocked_count.set(count)

    def update_syn_blocked_count(self, count: int):
        """Update SYN blocked IPs count"""
        self.syn_blocked_count.set(count)

    def update_l1_meter_count(self, count: int):
        """Update L1 meter configured count"""
        self.l1_meter_configured_count.set(count)

    def update_l2_meter_count(self, count: int):
        """Update L2 meter configured count"""
        self.l2_meter_configured_count.set(count)

    def update_global_counters(self, packet_count: int, drop_count: int):
        """Update global packet and drop counters"""
        self.global_packet_count.set(packet_count)
        self.global_drop_count.set(drop_count)

    def update_uptime(self):
        """Update controller uptime metric"""
        self.uptime_seconds.set(time.time() - self._start_time)

    # ML Metrics Methods
    def update_ml_model_trained(self, trained: bool):
        """Update ML model trained status"""
        self.ml_model_trained.set(1 if trained else 0)

    def update_ml_mode(self, mode: str):
        """Update ML operation mode"""
        mode_value = 1 if mode == 'auto' else 2
        self.ml_mode.set(mode_value)

    def record_ml_threshold_adjustment(self, threshold_type: str):
        """Record a threshold adjustment"""
        self.ml_threshold_adjustments_total.labels(threshold_type=threshold_type).inc()

    def update_ml_threshold(self, threshold_type: str, value: float):
        """Update current ML threshold value"""
        self.ml_current_threshold.labels(threshold_type=threshold_type).set(value)

    def update_ml_baseline(self, threshold_type: str, mean: float, std: float):
        """Update ML baseline statistics"""
        self.ml_baseline_mean.labels(threshold_type=threshold_type).set(mean)
        self.ml_baseline_std.labels(threshold_type=threshold_type).set(std)

    def update_ml_pending_approvals(self, count: int):
        """Update pending approvals count"""
        self.ml_pending_approvals.set(count)

    def update_ml_training_samples(self, count: int):
        """Update training samples count"""
        self.ml_training_samples.set(count)

    def get_drop_stats(self) -> Dict:
        """Get current drop statistics"""
        with self._lock:
            return {
                reason: {
                    'total': stats['total'],
                    'by_ip': dict(stats['by_ip'])
                }
                for reason, stats in self._drop_stats.items()
            }

    def get_meter_stats(self) -> Dict:
        """Get current meter statistics"""
        with self._lock:
            return {level: dict(protocols) for level, protocols in self._meter_stats.items()}

    def get_metrics(self) -> bytes:
        """Generate Prometheus metrics"""
        self.update_uptime()
        return generate_latest()

    def reset_stats(self):
        """Reset all statistics"""
        with self._lock:
            for reason in self._drop_stats:
                self._drop_stats[reason]['total'] = 0
                self._drop_stats[reason]['by_ip'].clear()
            for level in self._meter_stats:
                for protocol in self._meter_stats[level]:
                    self._meter_stats[level][protocol] = 0
