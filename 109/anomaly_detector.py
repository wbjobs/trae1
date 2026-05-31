import logging
import time
from collections import deque
from typing import Dict, Any, Optional
from feature_extractor import FeatureExtractor

logger = logging.getLogger(__name__)

class AnomalyDetector:
    def __init__(self, config, rule_manager, classifier=None):
        self.config = config
        self.rule_manager = rule_manager
        self.classifier = classifier
        self.traffic_history: Dict[str, deque] = {}
        self.syn_count: Dict[str, list] = {}
        self.rate_history: Dict[str, deque] = {}
        self.feature_extractors: Dict[str, FeatureExtractor] = {}
        self.ml_enabled = config.ml_enabled and classifier is not None
        
        if self.ml_enabled:
            logger.info("ML-based attack classification enabled")
        else:
            logger.info("ML-based attack classification disabled")
    
    def analyze(self, record, baseline_traffic, current_traffic):
        dst_ip = record.dst_ip
        self._update_traffic_history(dst_ip, record.bytes_count)
        self._update_feature_extractor(record)
        
        current_rate = self._calculate_rate_gbps(dst_ip)
        
        if self.ml_enabled and self._should_classify(dst_ip):
            attack_type, confidence, strategy = self._classify_attack(dst_ip)
            if attack_type != 'benign':
                logger.warning("ML-classified attack detected", extra={
                    "dst_ip": dst_ip,
                    "attack_type": attack_type,
                    "confidence": confidence,
                    "strategy": strategy.get('description', ''),
                    "rate_gbps": current_rate
                })
                self.rule_manager.trigger_diversion_with_strategy(
                    record, attack_type, current_rate, strategy
                )
                return
        
        if self._check_syn_flood(record):
            logger.warning("SYN Flood detected", extra={
                "dst_ip": dst_ip,
                "src_ip": record.src_ip,
                "rate_gbps": current_rate
            })
            self.rule_manager.trigger_diversion(record, "syn_flood", current_rate)
            return
        
        if self._check_traffic_surge(dst_ip, baseline_traffic, current_traffic):
            tier = self.config.get_tier_for_rate(current_rate)
            logger.warning("Traffic surge detected", extra={
                "dst_ip": dst_ip,
                "rate_gbps": current_rate,
                "tier_level": tier['level'],
                "tier_name": tier['name']
            })
            self.rule_manager.trigger_diversion(record, "traffic_surge", current_rate)
    
    def _update_feature_extractor(self, record):
        dst_ip = record.dst_ip
        if dst_ip not in self.feature_extractors:
            self.feature_extractors[dst_ip] = FeatureExtractor(
                window_size=self.config.ml_feature_window_seconds
            )
        
        self.feature_extractors[dst_ip].add_sample(
            src_ip=record.src_ip,
            dst_ip=record.dst_ip,
            protocol=record.protocol,
            src_port=record.src_port,
            dst_port=record.dst_port,
            packet_size=record.bytes_count,
            flags=record.flags
        )
    
    def _should_classify(self, dst_ip: str) -> bool:
        if dst_ip not in self.feature_extractors:
            return False
        
        extractor = self.feature_extractors[dst_ip]
        features = extractor.get_current_features()
        
        return features.get('num_packets', 0) > 100
    
    def _classify_attack(self, dst_ip: str):
        if not self.classifier or dst_ip not in self.feature_extractors:
            return 'benign', 0.0, {}
        
        features = self.feature_extractors[dst_ip].get_current_features()
        return self.classifier.classify_and_get_strategy(features)
    
    def get_classification_result(self, dst_ip: str) -> Optional[Dict]:
        if not self.classifier or dst_ip not in self.feature_extractors:
            return None
        
        features = self.feature_extractors[dst_ip].get_current_features()
        attack_type, confidence, strategy = self.classifier.classify_and_get_strategy(features)
        
        return {
            "dst_ip": dst_ip,
            "attack_type": attack_type,
            "confidence": confidence,
            "strategy": strategy,
            "features": features
        }
    
    def _update_traffic_history(self, dst_ip, bytes_count):
        current_time = time.time()
        if dst_ip not in self.traffic_history:
            self.traffic_history[dst_ip] = deque(maxlen=60)
        
        self.traffic_history[dst_ip].append((current_time, bytes_count))
    
    def _calculate_rate_gbps(self, dst_ip):
        if dst_ip not in self.traffic_history or len(self.traffic_history[dst_ip]) < 2:
            return 0.0
        
        history = list(self.traffic_history[dst_ip])
        total_bytes = sum(b for t, b in history)
        time_span = history[-1][0] - history[0][0]
        
        if time_span <= 0:
            return 0.0
        
        rate_gbps = (total_bytes * 8) / (time_span * 1e9)
        return rate_gbps
    
    def _check_syn_flood(self, record):
        if record.protocol != 6:
            return False
        
        tcp_flags = record.flags
        syn_flag = (tcp_flags & 0x02) != 0
        ack_flag = (tcp_flags & 0x10) != 0
        
        if syn_flag and not ack_flag:
            key = f"{record.dst_ip}"
            current_time = time.time()
            
            if key not in self.syn_count:
                self.syn_count[key] = []
            
            self.syn_count[key] = [t for t in self.syn_count[key] if current_time - t < 10]
            self.syn_count[key].append(current_time)
            
            if len(self.syn_count[key]) > 100:
                return True
        return False
    
    def _check_traffic_surge(self, dst_ip, baseline_traffic, current_traffic):
        if dst_ip not in baseline_traffic or len(baseline_traffic[dst_ip]) < 5:
            return False
        
        baseline = sum(b for t, b in baseline_traffic[dst_ip]) / len(baseline_traffic[dst_ip])
        current = current_traffic.get(dst_ip, 0)
        
        if current > baseline * self.config.traffic_multiplier and baseline > 0:
            logger.info("Traffic baseline comparison", extra={
                "dst_ip": dst_ip,
                "baseline_bytes": baseline,
                "current_bytes": current,
                "multiplier": self.config.traffic_multiplier,
                "current_rate_gbps": self._calculate_rate_gbps(dst_ip)
            })
            return True
        return False
    
    def get_current_features(self, dst_ip: str) -> Optional[Dict[str, float]]:
        if dst_ip not in self.feature_extractors:
            return None
        return self.feature_extractors[dst_ip].get_current_features()
