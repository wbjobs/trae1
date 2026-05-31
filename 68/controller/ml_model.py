"""
Isolation Forest Model for Dynamic Threshold Calculation
Computes adaptive thresholds based on normal traffic baseline
"""

import time
import threading
import logging
import numpy as np
from collections import deque
from typing import Dict, List, Optional, Tuple
from enum import Enum

logger = logging.getLogger(__name__)

try:
    from sklearn.ensemble import IsolationForest
    SKLEARN_AVAILABLE = True
except ImportError:
    SKLEARN_AVAILABLE = False
    logger.warning("scikit-learn not available, using fallback statistical method")


class ModelMode(Enum):
    AUTO = "auto"
    MANUAL = "manual"


class ThresholdType(Enum):
    UDP_RATE = "udp_rate"
    ICMP_RATE = "icmp_rate"
    SYN_RATE = "syn_rate"
    CONNECTION_RATE = "connection_rate"
    PACKET_RATE = "packet_rate"


class DynamicThreshold:
    """Represents a dynamically computed threshold"""
    
    def __init__(self, threshold_type: ThresholdType, value: float,
                 baseline_mean: float, baseline_std: float,
                 confidence: float = 3.0):
        self.threshold_type = threshold_type
        self.value = value
        self.baseline_mean = baseline_mean
        self.baseline_std = baseline_std
        self.confidence = confidence
        self.computed_at = time.time()
        self.applied = False
        self.approved = False


class IsolationForestModel:
    """Isolation Forest model for anomaly detection and threshold calculation"""

    def __init__(self, n_estimators: int = 100, contamination: float = 0.1,
                 max_samples: str = "auto", random_state: int = 42):
        self._n_estimators = n_estimators
        self._contamination = contamination
        self._max_samples = max_samples
        self._random_state = random_state

        self._model: Optional[IsolationForest] = None
        self._is_trained = False

        self._training_data: Dict[str, deque] = {
            'packet_rates': deque(maxlen=1000),
            'connection_rates': deque(maxlen=1000),
            'udp_rates': deque(maxlen=1000),
            'icmp_rates': deque(maxlen=1000),
            'syn_rates': deque(maxlen=1000),
        }

        self._lock = threading.Lock()
        self._last_trained = 0
        self._retrain_interval = 300

        self._thresholds: Dict[ThresholdType, DynamicThreshold] = {}
        self._pending_thresholds: Dict[ThresholdType, DynamicThreshold] = {}

        self._mode = ModelMode.AUTO

    def set_mode(self, mode: ModelMode):
        """Set operation mode"""
        self._mode = mode
        logger.info(f"Model mode set to: {mode.value}")

    def get_mode(self) -> ModelMode:
        """Get current operation mode"""
        return self._mode

    def add_training_data(self, features: Dict[str, List[float]]):
        """Add training data for model retraining"""
        with self._lock:
            for key, values in features.items():
                if key in self._training_data:
                    self._training_data[key].extend(values)

            logger.debug(f"Training data updated. Total samples: "
                        f"{len(self._training_data['packet_rates'])}")

    def train(self, force: bool = False) -> bool:
        """Train or retrain the isolation forest model"""
        with self._lock:
            now = time.time()

            if not force and (now - self._last_trained) < self._retrain_interval:
                return False

            if len(self._training_data['packet_rates']) < 10:
                logger.warning("Not enough training data. Need at least 10 samples")
                return False

            try:
                if SKLEARN_AVAILABLE:
                    self._train_sklearn()
                else:
                    self._train_statistical()

                self._last_trained = now
                self._is_trained = True
                logger.info("Model trained successfully")
                return True

            except Exception as e:
                logger.error(f"Model training failed: {e}")
                return False

    def _train_sklearn(self):
        """Train using scikit-learn IsolationForest"""
        feature_matrix = self._build_feature_matrix()

        self._model = IsolationForest(
            n_estimators=self._n_estimators,
            contamination=self._contamination,
            max_samples=self._max_samples,
            random_state=self._random_state,
            n_jobs=-1
        )

        self._model.fit(feature_matrix)

        anomaly_scores = self._model.decision_function(feature_matrix)
        self._compute_thresholds(anomaly_scores)

    def _train_statistical(self):
        """Fallback statistical method when sklearn not available"""
        for threshold_type in ThresholdType:
            key = self._get_data_key(threshold_type)
            if key in self._training_data and len(self._training_data[key]) > 0:
                data = list(self._training_data[key])
                mean = np.mean(data)
                std = np.std(data)

                threshold_value = mean + 3 * std
                threshold = DynamicThreshold(
                    threshold_type=threshold_type,
                    value=threshold_value,
                    baseline_mean=mean,
                    baseline_std=std,
                    confidence=3.0
                )

                self._thresholds[threshold_type] = threshold

    def _build_feature_matrix(self) -> np.ndarray:
        """Build feature matrix from training data"""
        features = []

        for key in ['packet_rates', 'connection_rates', 'udp_rates',
                     'icmp_rates', 'syn_rates']:
            data = list(self._training_data[key])
            if len(data) > 0:
                features.append(data)

        min_len = min(len(f) for f in features)
        if min_len > 0:
            features = [f[:min_len] for f in features]
            return np.column_stack(features)

        return np.array([]).reshape(0, 0)

    def _get_data_key(self, threshold_type: ThresholdType) -> str:
        """Get training data key for threshold type"""
        mapping = {
            ThresholdType.UDP_RATE: 'udp_rates',
            ThresholdType.ICMP_RATE: 'icmp_rates',
            ThresholdType.SYN_RATE: 'syn_rates',
            ThresholdType.CONNECTION_RATE: 'connection_rates',
            ThresholdType.PACKET_RATE: 'packet_rates',
        }
        return mapping.get(threshold_type, 'packet_rates')

    def _compute_thresholds(self, anomaly_scores: np.ndarray):
        """Compute dynamic thresholds from model"""
        if len(anomaly_scores) == 0:
            return

        for threshold_type in ThresholdType:
            key = self._get_data_key(threshold_type)
            if key in self._training_data and len(self._training_data[key]) > 0:
                data = list(self._training_data[key])
                mean = np.mean(data)
                std = np.std(data)

                confidence = self._calculate_confidence(anomaly_scores)
                threshold_value = mean + confidence * std

                threshold = DynamicThreshold(
                    threshold_type=threshold_type,
                    value=threshold_value,
                    baseline_mean=mean,
                    baseline_std=std,
                    confidence=confidence
                )

                if self._mode == ModelMode.AUTO:
                    self._thresholds[threshold_type] = threshold
                    threshold.approved = True
                    threshold.applied = True
                else:
                    self._pending_thresholds[threshold_type] = threshold

    def _calculate_confidence(self, anomaly_scores: np.ndarray) -> float:
        """Calculate confidence multiplier based on anomaly score distribution"""
        if len(anomaly_scores) == 0:
            return 3.0

        q25, q75 = np.percentile(anomaly_scores, [25, 75])
        iqr = q75 - q25

        if iqr == 0:
            return 3.0

        score_range = np.max(anomaly_scores) - np.min(anomaly_scores)
        normalized_iqr = iqr / score_range if score_range > 0 else 0

        if normalized_iqr < 0.3:
            return 2.0
        elif normalized_iqr < 0.5:
            return 2.5
        elif normalized_iqr < 0.7:
            return 3.0
        else:
            return 3.5

    def get_threshold(self, threshold_type: ThresholdType) -> Optional[DynamicThreshold]:
        """Get current threshold for a type"""
        with self._lock:
            return self._thresholds.get(threshold_type)

    def get_all_thresholds(self) -> Dict[str, Dict]:
        """Get all current thresholds"""
        with self._lock:
            result = {}
            for threshold_type, threshold in self._thresholds.items():
                result[threshold_type.value] = {
                    'value': threshold.value,
                    'baseline_mean': threshold.baseline_mean,
                    'baseline_std': threshold.baseline_std,
                    'confidence': threshold.confidence,
                    'computed_at': threshold.computed_at,
                    'applied': threshold.applied
                }
            return result

    def get_pending_thresholds(self) -> Dict[str, Dict]:
        """Get pending thresholds awaiting approval"""
        with self._lock:
            result = {}
            for threshold_type, threshold in self._pending_thresholds.items():
                result[threshold_type.value] = {
                    'value': threshold.value,
                    'baseline_mean': threshold.baseline_mean,
                    'baseline_std': threshold.baseline_std,
                    'confidence': threshold.confidence,
                    'computed_at': threshold.computed_at
                }
            return result

    def approve_threshold(self, threshold_type: ThresholdType) -> bool:
        """Approve a pending threshold"""
        with self._lock:
            if threshold_type in self._pending_thresholds:
                threshold = self._pending_thresholds.pop(threshold_type)
                threshold.approved = True
                threshold.applied = True
                self._thresholds[threshold_type] = threshold
                logger.info(f"Threshold approved: {threshold_type.value}")
                return True
            return False

    def reject_threshold(self, threshold_type: ThresholdType) -> bool:
        """Reject a pending threshold"""
        with self._lock:
            if threshold_type in self._pending_thresholds:
                del self._pending_thresholds[threshold_type]
                logger.info(f"Threshold rejected: {threshold_type.value}")
                return True
            return False

    def get_model_status(self) -> Dict:
        """Get model status"""
        return {
            'is_trained': self._is_trained,
            'last_trained': self._last_trained,
            'mode': self._mode.value,
            'training_samples': {
                key: len(data) for key, data in self._training_data.items()
            },
            'sklearn_available': SKLEARN_AVAILABLE,
            'retrain_interval': self._retrain_interval
        }

    def is_trained(self) -> bool:
        """Check if model is trained"""
        return self._is_trained
