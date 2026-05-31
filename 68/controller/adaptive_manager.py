"""
Adaptive Threshold Manager for DDoS Defense Gateway
Coordinates traffic collection, ML model, and dynamic P4 table updates
"""

import time
import threading
import logging
from typing import Dict, List, Optional, Callable
from enum import Enum

from .traffic_collector import TrafficCollector
from .ml_model import IsolationForestModel, ThresholdType, ModelMode, DynamicThreshold
from .p4_controller import P4Controller, ProtocolType

logger = logging.getLogger(__name__)


class AdaptiveManager:
    """Manages adaptive threshold adjustment based on ML model"""

    def __init__(self, p4_controller: P4Controller,
                 traffic_collector: TrafficCollector,
                 ml_model: IsolationForestModel):
        self.p4_controller = p4_controller
        self.traffic_collector = traffic_collector
        self.ml_model = ml_model

        self._lock = threading.Lock()
        self._running = False

        self._monitor_thread: Optional[threading.Thread] = None
        self._training_thread: Optional[threading.Thread] = None

        self._threshold_change_callbacks: List[Callable] = []
        self._adjustment_count = 0
        self._last_adjustment = 0

        self._current_thresholds: Dict[ThresholdType, float] = {
            ThresholdType.UDP_RATE: 100,
            ThresholdType.ICMP_RATE: 50,
            ThresholdType.SYN_RATE: 200,
            ThresholdType.CONNECTION_RATE: 50,
            ThresholdType.PACKET_RATE: 500,
        }

        self._min_thresholds: Dict[ThresholdType, float] = {
            ThresholdType.UDP_RATE: 20,
            ThresholdType.ICMP_RATE: 10,
            ThresholdType.SYN_RATE: 50,
            ThresholdType.CONNECTION_RATE: 10,
            ThresholdType.PACKET_RATE: 100,
        }

        self._max_thresholds: Dict[ThresholdType, float] = {
            ThresholdType.UDP_RATE: 1000,
            ThresholdType.ICMP_RATE: 500,
            ThresholdType.SYN_RATE: 2000,
            ThresholdType.CONNECTION_RATE: 500,
            ThresholdType.PACKET_RATE: 5000,
        }

    def start(self):
        """Start the adaptive threshold manager"""
        self._running = True

        self._monitor_thread = threading.Thread(
            target=self._monitor_loop, daemon=True
        )
        self._monitor_thread.start()

        self._training_thread = threading.Thread(
            target=self._training_loop, daemon=True
        )
        self._training_thread.start()

        logger.info("Adaptive threshold manager started")

    def stop(self):
        """Stop the adaptive threshold manager"""
        self._running = False
        if self._monitor_thread:
            self._monitor_thread.join(timeout=5)
        if self._training_thread:
            self._training_thread.join(timeout=5)
        logger.info("Adaptive threshold manager stopped")

    def _monitor_loop(self):
        """Main monitoring loop"""
        while self._running:
            try:
                time.sleep(10)
                self._check_and_adjust()
            except Exception as e:
                logger.error(f"Monitor loop error: {e}")

    def _training_loop(self):
        """Model retraining loop"""
        while self._running:
            try:
                time.sleep(300)
                self._retrain_model()
            except Exception as e:
                logger.error(f"Training loop error: {e}")

    def _check_and_adjust(self):
        """Check current traffic against thresholds and adjust if needed"""
        if not self.ml_model.is_trained():
            return

        current_stats = self.traffic_collector.get_current_stats()
        dynamic_thresholds = self.ml_model.get_all_thresholds()

        adjustments = []

        for threshold_type in ThresholdType:
            if threshold_type.value not in dynamic_thresholds:
                continue

            dynamic_threshold = dynamic_thresholds[threshold_type.value]
            current_value = self._current_thresholds.get(threshold_type)

            if current_value is None:
                continue

            new_value = dynamic_threshold['value']

            min_val = self._min_thresholds.get(threshold_type, current_value * 0.2)
            max_val = self._max_thresholds.get(threshold_type, current_value * 5.0)

            new_value = max(min_val, min(new_value, max_val))

            change_percent = abs(new_value - current_value) / current_value * 100

            if change_percent > 10:
                adjustments.append({
                    'type': threshold_type,
                    'old_value': current_value,
                    'new_value': new_value,
                    'change_percent': change_percent
                })

        if adjustments and self.ml_model.get_mode() == ModelMode.AUTO:
            self._apply_adjustments(adjustments)

    def _apply_adjustments(self, adjustments: List[Dict]):
        """Apply threshold adjustments to P4 tables"""
        with self._lock:
            for adj in adjustments:
                threshold_type = adj['type']
                new_value = int(adj['new_value'])

                self._current_thresholds[threshold_type] = new_value
                self._adjustment_count += 1
                self._last_adjustment = time.time()

                self._update_p4_tables(threshold_type, new_value)

                logger.info(f"Threshold adjusted: {threshold_type.value} "
                          f"{adj['old_value']:.0f} -> {new_value} "
                          f"({adj['change_percent']:.1f}%)")

                for cb in self._threshold_change_callbacks:
                    try:
                        cb(threshold_type, adj['old_value'], new_value)
                    except Exception as e:
                        logger.error(f"Callback error: {e}")

    def _update_p4_tables(self, threshold_type: ThresholdType, value: int):
        """Update P4 tables with new threshold"""
        protocol_map = {
            ThresholdType.UDP_RATE: ProtocolType.UDP,
            ThresholdType.ICMP_RATE: ProtocolType.ICMP,
            ThresholdType.SYN_RATE: ProtocolType.TCP_SYN,
        }

        if threshold_type in protocol_map:
            protocol = protocol_map[threshold_type]
            l1_stats = self.p4_controller.get_l1_meter_stats()

            for entry in l1_stats:
                if entry.get('protocol') == protocol.name:
                    src_ip = entry.get('src_ip')
                    if src_ip:
                        try:
                            self.p4_controller.configure_l1_meter(
                                src_ip, protocol, value
                            )
                        except Exception as e:
                            logger.error(f"Failed to update L1 meter for {src_ip}: {e}")

    def _retrain_model(self):
        """Retrain the ML model with latest data"""
        features = self.traffic_collector.get_all_features()

        training_data = {
            'packet_rates': features.get('packet_rates', []),
            'connection_rates': features.get('connection_rates', []),
            'udp_rates': [],
            'icmp_rates': [],
            'syn_rates': [],
        }

        for ip_stats in self.traffic_collector.get_top_talkers(limit=100):
            if ip_stats['udp_packets'] > 0:
                training_data['udp_rates'].append(ip_stats['udp_packets'])
            if ip_stats['icmp_packets'] > 0:
                training_data['icmp_rates'].append(ip_stats['icmp_packets'])
            if ip_stats['syn_packets'] > 0:
                training_data['syn_rates'].append(ip_stats['syn_packets'])

        self.ml_model.add_training_data(training_data)
        self.ml_model.train()

        if self.ml_model.get_mode() == ModelMode.AUTO:
            self._apply_dynamic_thresholds()

    def _apply_dynamic_thresholds(self):
        """Apply all dynamic thresholds from model"""
        dynamic_thresholds = self.ml_model.get_all_thresholds()

        for type_name, threshold in dynamic_thresholds.items():
            try:
                threshold_type = ThresholdType(type_name)
            except ValueError:
                continue

            new_value = int(threshold['value'])
            current = self._current_thresholds.get(threshold_type)

            if current is not None and abs(new_value - current) / current > 0.1:
                self._current_thresholds[threshold_type] = new_value
                self._update_p4_tables(threshold_type, new_value)
                self._adjustment_count += 1

    def register_threshold_change_callback(self, callback: Callable):
        """Register callback for threshold changes"""
        self._threshold_change_callbacks.append(callback)

    def get_current_thresholds(self) -> Dict[str, float]:
        """Get current threshold values"""
        with self._lock:
            return {
                type.value: value
                for type, value in self._current_thresholds.items()
            }

    def get_threshold_bounds(self) -> Dict[str, Dict]:
        """Get threshold min/max bounds"""
        return {
            type.value: {
                'min': self._min_thresholds[type],
                'max': self._max_thresholds[type],
                'current': self._current_thresholds[type]
            }
            for type in ThresholdType
        }

    def set_threshold_bounds(self, threshold_type: ThresholdType,
                             min_val: float, max_val: float):
        """Set threshold bounds"""
        with self._lock:
            self._min_thresholds[threshold_type] = min_val
            self._max_thresholds[threshold_type] = max_val

    def get_status(self) -> Dict:
        """Get adaptive manager status"""
        return {
            'running': self._running,
            'mode': self.ml_model.get_mode().value,
            'adjustment_count': self._adjustment_count,
            'last_adjustment': self._last_adjustment,
            'current_thresholds': self.get_current_thresholds(),
            'model_status': self.ml_model.get_model_status(),
            'traffic_stats': self.traffic_collector.get_current_stats()
        }

    def force_retrain(self):
        """Force immediate model retraining"""
        logger.info("Force retraining model...")
        self._retrain_model()
