import os
import json
import time
import pickle
import logging
from typing import Dict, List, Tuple, Optional
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, accuracy_score
import numpy as np

logger = logging.getLogger(__name__)

ATTACK_TYPES = [
    'benign',
    'syn_flood',
    'udp_amplification',
    'http_flood',
    'dns_query_flood',
    'ntp_reflection'
]

ATTACK_STRATEGY_MAPPING = {
    'benign': {
        'action': 'none',
        'description': 'Normal traffic, no action needed'
    },
    'syn_flood': {
        'action': 'redirect',
        'tier': 1,
        'description': 'SYN Flood - redirect to scrubbing center for TCP state tracking'
    },
    'udp_amplification': {
        'action': 'discard',
        'tier': 3,
        'description': 'UDP Amplification - discard at edge, no redirect needed'
    },
    'http_flood': {
        'action': 'redirect',
        'tier': 2,
        'description': 'HTTP Flood - redirect to WAF for request inspection'
    },
    'dns_query_flood': {
        'action': 'rate-limit',
        'tier': 2,
        'description': 'DNS Query Flood - rate limit DNS queries'
    },
    'ntp_reflection': {
        'action': 'discard',
        'tier': 3,
        'description': 'NTP Reflection - discard UDP at edge'
    }
}

class AttackClassifier:
    def __init__(self, config):
        self.config = config
        self.model = None
        self.feature_names = None
        self.model_path = config.get('ml', {}).get('model_path', 'attack_classifier.pkl')
        self.threshold = config.get('ml', {}).get('confidence_threshold', 0.7)
        self.retrain_interval = config.get('ml', {}).get('retrain_interval_days', 7)
        self.target_accuracy = config.get('ml', {}).get('target_accuracy', 0.95)
        self.inference_timeout = config.get('ml', {}).get('inference_timeout_ms', 3000)
        self.is_trained = False
    
    def initialize(self):
        if os.path.exists(self.model_path):
            self.load_model()
        else:
            logger.warning("No trained model found, generating synthetic training data")
            self._generate_synthetic_training_data()
            self.train()
    
    def _generate_synthetic_training_data(self):
        logger.info("Generating synthetic training data for initial model")
        
        np.random.seed(42)
        n_samples = 10000
        
        X = []
        y = []
        
        base_features = {
            'benign': self._generate_benign_features,
            'syn_flood': self._generate_syn_flood_features,
            'udp_amplification': self._generate_udp_amplification_features,
            'http_flood': self._generate_http_flood_features,
            'dns_query_flood': self._generate_dns_query_flood_features,
            'ntp_reflection': self._generate_ntp_reflection_features
        }
        
        for attack_type, gen_func in base_features.items():
            n_type_samples = n_samples // len(ATTACK_TYPES)
            for _ in range(n_type_samples):
                features = gen_func()
                X.append(features)
                y.append(attack_type)
        
        X = np.array(X)
        y = np.array(y)
        
        self.feature_names = self._get_feature_names()
        
        if len(X.shape) == 1:
            X = X.reshape(-1, 1)
        
        X_train, X_test, y_train, y_test = train_test_split(
            X, y, test_size=0.2, random_state=42, stratify=y
        )
        
        self.model = RandomForestClassifier(
            n_estimators=100,
            max_depth=15,
            min_samples_split=5,
            min_samples_leaf=2,
            random_state=42,
            n_jobs=-1
        )
        
        self.model.fit(X_train, y_train)
        
        y_pred = self.model.predict(X_test)
        accuracy = accuracy_score(y_test, y_pred)
        
        logger.info("Synthetic training completed", extra={"accuracy": accuracy})
        
        if accuracy < self.target_accuracy:
            logger.warning("Model accuracy below target", extra={
                "current": accuracy,
                "target": self.target_accuracy
            })
        
        self.is_trained = True
        self.save_model()
    
    def _generate_benign_features(self) -> List[float]:
        features = []
        features.append(np.random.randint(10, 100))
        features.append(np.random.randint(1, 5))
        features.append(np.random.randint(1, 50))
        features.append(np.random.randint(1, 10))
        features.append(np.random.uniform(200, 800))
        features.append(np.random.uniform(50, 200))
        features.append(np.random.randint(100, 500))
        features.append(np.random.randint(500, 1500))
        features.append(0.4 + np.random.uniform(-0.1, 0.1))
        features.append(0.3 + np.random.uniform(-0.1, 0.1))
        features.append(0.1 + np.random.uniform(-0.05, 0.05))
        features.append(np.random.uniform(3, 5))
        features.append(np.random.uniform(3, 5))
        features.append(np.random.uniform(3, 5))
        features.append(np.random.uniform(3, 5))
        features.append(np.random.uniform(10, 30))
        features.append(np.random.uniform(5, 15))
        features.append(np.random.uniform(10, 50))
        features.append(np.random.uniform(5, 20))
        features.append(np.random.uniform(0.3, 0.7))
        features.append(np.random.uniform(0.3, 0.7))
        features.append(np.random.uniform(0.01, 0.05))
        features.append(np.random.uniform(0.01, 0.05))
        features.append(np.random.uniform(0.01, 0.05))
        features.append(np.random.uniform(0.0, 0.02))
        features.append(np.random.uniform(0.0, 0.02))
        features.append(np.random.uniform(0.1, 0.3))
        features.append(np.random.uniform(0.0, 0.1))
        features.append(np.random.uniform(0, 60))
        features.append(np.random.uniform(50, 500))
        features.append(np.random.uniform(1000, 10000))
        features.append(np.random.uniform(0.5, 2))
        return features
    
    def _generate_syn_flood_features(self) -> List[float]:
        base = self._generate_benign_features()
        base[26] = 0.8 + np.random.uniform(0, 0.15)
        base[27] = 0.1 + np.random.uniform(0, 0.1)
        base[28] = 5 + np.random.uniform(0, 10)
        base[0] = np.random.randint(500, 2000)
        base[29] = np.random.uniform(5, 15)
        base[30] = np.random.uniform(5000, 50000)
        base[31] = np.random.uniform(50, 100)
        return base
    
    def _generate_udp_amplification_features(self) -> List[float]:
        base = self._generate_benign_features()
        base[20] = 0.0
        base[21] = 0.95 + np.random.uniform(0, 0.05)
        base[22] = 0.0
        base[0] = np.random.randint(1000, 5000)
        base[4] = np.random.uniform(300, 600)
        base[23] = np.random.uniform(0.5, 2)
        base[29] = np.random.uniform(100, 200)
        base[30] = np.random.uniform(50000, 500000)
        base[31] = np.random.uniform(200, 500)
        return base
    
    def _generate_http_flood_features(self) -> List[float]:
        base = self._generate_benign_features()
        base[20] = 0.7 + np.random.uniform(0, 0.2)
        base[21] = 0.2 + np.random.uniform(0, 0.1)
        base[25] = 0.5 + np.random.uniform(0, 0.3)
        base[0] = np.random.randint(500, 3000)
        base[29] = np.random.uniform(30, 60)
        base[30] = np.random.uniform(10000, 100000)
        base[31] = np.random.uniform(5, 15)
        return base
    
    def _generate_dns_query_flood_features(self) -> List[float]:
        base = self._generate_benign_features()
        base[21] = 0.3 + np.random.uniform(0, 0.2)
        base[24] = 0.6 + np.random.uniform(0, 0.2)
        base[0] = np.random.randint(500, 2000)
        base[29] = np.random.uniform(20, 40)
        base[30] = np.random.uniform(20000, 200000)
        base[31] = np.random.uniform(20, 50)
        return base
    
    def _generate_ntp_reflection_features(self) -> List[float]:
        base = self._generate_benign_features()
        base[21] = 0.95 + np.random.uniform(0, 0.05)
        base[23] = 0.9 + np.random.uniform(0, 0.1)
        base[0] = np.random.randint(2000, 10000)
        base[4] = np.random.uniform(400, 600))
        base[29] = np.random.uniform(50, 100)
        base[30] = np.random.uniform(100000, 1000000)
        base[31] = np.random.uniform(100, 500)
        return base
    
    def _get_feature_names(self) -> List[str]:
        return [
            'num_packets', 'num_flows', 'num_src_ips', 'num_dst_ips',
            'packet_size_mean', 'packet_size_std', 'packet_size_min', 'packet_size_max',
            'protocol_tcp_ratio', 'protocol_udp_ratio', 'protocol_icmp_ratio',
            'src_ip_entropy', 'dst_ip_entropy', 'src_port_entropy', 'dst_port_entropy',
            'flow_duration_mean', 'flow_duration_std',
            'packets_per_flow_mean', 'packets_per_flow_std',
            'bytes_per_flow_mean', 'bytes_per_flow_std',
            'syn_ratio', 'ack_ratio', 'fin_ratio', 'rst_ratio', 'syn_ack_ratio',
            'dns_query_ratio', 'http_ratio', 'https_ratio', 'ntp_ratio',
            'time_span', 'packet_rate', 'bytes_per_second', 'unique_src_ips_per_flow'
        ]
    
    def classify(self, features: Dict[str, float]) -> Tuple[str, float, Dict]:
        start_time = time.time()
        
        if self.model is None:
            logger.warning("Model not loaded, returning benign")
            return 'benign', 0.0, {}
        
        feature_vector = []
        for name in self.feature_names:
            feature_vector.append(features.get(name, 0.0))
        
        feature_vector = np.array(feature_vector).reshape(1, -1)
        
        try:
            probabilities = self.model.predict_proba(feature_vector)[0]
            predicted_class_idx = np.argmax(probabilities)
            confidence = probabilities[predicted_class_idx]
            predicted_class = self.model.classes_[predicted_class_idx]
            
            elapsed_ms = (time.time() - start_time) * 1000
            
            if elapsed_ms > self.inference_timeout:
                logger.warning("Inference timeout exceeded", extra={
                    "elapsed_ms": elapsed_ms,
                    "threshold_ms": self.inference_timeout
                })
            
            if confidence < self.threshold:
                logger.info("Low confidence classification", extra={
                    "predicted": predicted_class,
                    "confidence": confidence,
                    "threshold": self.threshold
                })
                predicted_class = 'benign'
                confidence = 1.0 - confidence
            
            strategy = ATTACK_STRATEGY_MAPPING.get(predicted_class, ATTACK_STRATEGY_MAPPING['benign'])
            
            logger.info("Classification result", extra={
                "attack_type": predicted_class,
                "confidence": confidence,
                "inference_time_ms": elapsed_ms
            })
            
            return predicted_class, confidence, strategy
            
        except Exception as e:
            logger.error("Classification error", extra={"error": str(e)})
            return 'benign', 0.0, {}
    
    def train(self):
        logger.info("Training model with collected data")
        
        if not hasattr(self, '_training_data'):
            logger.warning("No training data available")
            return False
        
        X = np.array(self._training_data['X'])
        y = np.array(self._training_data['y'])
        
        if len(X.shape) == 1:
            X = X.reshape(-1, 1)
        
        X_train, X_test, y_train, y_test = train_test_split(
            X, y, test_size=0.2, random_state=42, stratify=y
        )
        
        self.model = RandomForestClassifier(
            n_estimators=100,
            max_depth=15,
            min_samples_split=5,
            min_samples_leaf=2,
            random_state=42,
            n_jobs=-1
        )
        
        self.model.fit(X_train, y_train)
        
        y_pred = self.model.predict(X_test)
        accuracy = accuracy_score(y_test, y_pred)
        
        logger.info("Model training completed", extra={
            "accuracy": accuracy,
            "target": self.target_accuracy
        })
        
        if accuracy >= self.target_accuracy:
            self.is_trained = True
            self.save_model()
            return True
        
        return False
    
    def save_model(self):
        try:
            with open(self.model_path, 'wb') as f:
                pickle.dump({
                    'model': self.model,
                    'feature_names': self.feature_names,
                    'is_trained': self.is_trained
                }, f)
            logger.info("Model saved", extra={"path": self.model_path})
        except Exception as e:
            logger.error("Failed to save model", extra={"error": str(e)})
    
    def load_model(self):
        try:
            with open(self.model_path, 'rb') as f:
                data = pickle.load(f)
                self.model = data['model']
                self.feature_names = data['feature_names']
                self.is_trained = data.get('is_trained', True)
            logger.info("Model loaded", extra={"path": self.model_path})
        except Exception as e:
            logger.error("Failed to load model", extra={"error": str(e)})
    
    def add_training_sample(self, features: Dict[str, float], label: str):
        if not hasattr(self, '_training_data'):
            self._training_data = {'X': [], 'y': []}
        
        feature_vector = [features.get(name, 0.0) for name in self.feature_names]
        self._training_data['X'].append(feature_vector)
        self._training_data['y'].append(label)
    
    def get_strategy_for_attack(self, attack_type: str) -> Dict:
        return ATTACK_STRATEGY_MAPPING.get(attack_type, ATTACK_STRATEGY_MAPPING['benign'])
    
    def classify_and_get_strategy(self, features: Dict[str, float]) -> Tuple[str, float, Dict]:
        attack_type, confidence, strategy = self.classify(features)
        recommended_strategy = self.get_strategy_for_attack(attack_type)
        return attack_type, confidence, recommended_strategy
