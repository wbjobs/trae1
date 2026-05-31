import logging
import math
from collections import deque
from typing import Dict, Any, List, Optional, Tuple
from config import settings

logger = logging.getLogger(__name__)


class KalmanFilter:
    def __init__(self, process_noise: float = None, measurement_noise: float = None):
        self.process_noise = process_noise or settings.KALMAN_PROCESS_NOISE
        self.measurement_noise = measurement_noise or settings.KALMAN_MEASUREMENT_NOISE
        self.estimate = 0.0
        self.error_estimate = 1.0
        self.initialized = False

    def update(self, measurement: float) -> float:
        if not self.initialized:
            self.estimate = measurement
            self.initialized = True
            return measurement

        kalman_gain = self.error_estimate / (self.error_estimate + self.measurement_noise)
        self.estimate = self.estimate + kalman_gain * (measurement - self.estimate)
        self.error_estimate = (1.0 - kalman_gain) * self.error_estimate + self.process_noise
        return self.estimate

    def reset(self):
        self.estimate = 0.0
        self.error_estimate = 1.0
        self.initialized = False


class EMASmoother:
    def __init__(self, alpha: float = None):
        self.alpha = alpha if alpha is not None else settings.NOISE_REDUCTION_EMA_ALPHA
        self.ema_value = None

    def update(self, value: float) -> float:
        if self.ema_value is None:
            self.ema_value = value
        else:
            self.ema_value = self.alpha * value + (1 - self.alpha) * self.ema_value
        return self.ema_value

    def reset(self):
        self.ema_value = None


class SlidingWindowSmoother:
    def __init__(self, window_size: int = 5):
        self.window_size = window_size
        self.values = deque(maxlen=window_size)

    def update(self, value: float) -> float:
        self.values.append(value)
        if len(self.values) == 0:
            return value
        return sum(self.values) / len(self.values)

    def get_median(self) -> float:
        if not self.values:
            return 0.0
        sorted_vals = sorted(self.values)
        return sorted_vals[len(sorted_vals) // 2]

    def reset(self):
        self.values.clear()


class SavitzkyGolaySmoother:
    def __init__(self, window_size: int = 7, poly_order: int = 2):
        self.window_size = window_size
        self.poly_order = poly_order
        self.values = deque(maxlen=window_size)
        self._coeffs = None

    def _compute_coeffs(self):
        half = self.window_size // 2
        x = list(range(-half, half + 1))
        n = len(x)
        A = [[x_i ** j for j in range(self.poly_order + 1)] for x_i in x]
        ATA = [[sum(A[i][k] * A[j][k] for k in range(n)) for j in range(self.poly_order + 1)] for i in range(self.poly_order + 1)]
        ATA_inv = self._invert_matrix(ATA)
        self._coeffs = [sum(ATA_inv[0][k] * A[i][k] for k in range(self.poly_order + 1)) for i in range(n)]

    @staticmethod
    def _invert_matrix(matrix: List[List[float]]) -> List[List[float]]:
        n = len(matrix)
        aug = [row[:] + [1.0 if i == j else 0.0 for j in range(n)] for i, row in enumerate(matrix)]
        for col in range(n):
            pivot = aug[col][col]
            if abs(pivot) < 1e-10:
                for row in range(col + 1, n):
                    if abs(aug[row][col]) > abs(pivot):
                        aug[col], aug[row] = aug[row], aug[col]
                        pivot = aug[col][col]
                        break
            if abs(pivot) < 1e-10:
                return [[1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]
            for row in range(col + 1):
                aug[col][row] /= pivot
            for row in range(n):
                if row != col:
                    factor = aug[row][col]
                    for j in range(2 * n):
                        aug[row][j] -= factor * aug[col][j]
        return [row[n:] for row in aug]

    def update(self, value: float) -> float:
        if self._coeffs is None:
            self._compute_coeffs()
        self.values.append(value)
        if len(self.values) < self.window_size:
            return value
        smoothed = sum(c * v for c, v in zip(self._coeffs, self.values))
        return smoothed

    def reset(self):
        self.values.clear()


class DataFilter:
    _kalman_filters: Dict[str, KalmanFilter] = {}
    _ema_smoothers: Dict[str, EMASmoother] = {}
    _sliding_windows: Dict[str, SlidingWindowSmoother] = {}
    _sg_smoothers: Dict[str, SavitzkyGolaySmoother] = {}

    @staticmethod
    def filter_none_values(data: Dict[str, Any]) -> Dict[str, Any]:
        return {k: v for k, v in data.items() if v is not None}

    @staticmethod
    def filter_by_value_range(data: Dict[str, Any], field: str,
                              min_value: Optional[float] = None,
                              max_value: Optional[float] = None) -> bool:
        if field not in data:
            return False
        value = data[field]
        if not isinstance(value, (int, float)):
            return False
        if min_value is not None and value < min_value:
            return False
        if max_value is not None and value > max_value:
            return False
        return True

    @staticmethod
    def sanitize_metric_name(name: str) -> str:
        invalid_chars = [' ', ',', ';', '\\', '"', "'", '`', '=', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '+', '[', ']', '{', '}', '|', ':', '<', '>', '?', '/']
        result = name
        for char in invalid_chars:
            result = result.replace(char, '_')
        return result.lower()

    @staticmethod
    def deduplicate_metrics(metrics_list: List[Dict], key_fields: List[str]) -> List[Dict]:
        seen = set()
        unique_metrics = []
        for metric in metrics_list:
            key = tuple(metric.get(field, '') for field in key_fields)
            if key not in seen:
                seen.add(key)
                unique_metrics.append(metric)
        return unique_metrics

    @staticmethod
    def smooth_data(values: List[float], window: int = 3) -> List[float]:
        if len(values) < window:
            return values
        smoothed = []
        for i in range(len(values)):
            start = max(0, i - window // 2)
            end = min(len(values), i + window // 2 + 1)
            window_values = values[start:end]
            smoothed.append(sum(window_values) / len(window_values))
        return smoothed

    @staticmethod
    def smooth_data_median(values: List[float], window: int = 3) -> List[float]:
        if len(values) < window:
            return values
        smoothed = []
        for i in range(len(values)):
            start = max(0, i - window // 2)
            end = min(len(values), i + window // 2 + 1)
            window_values = sorted(values[start:end])
            smoothed.append(window_values[len(window_values) // 2])
        return smoothed

    @staticmethod
    def remove_outliers_iqr(values: List[float]) -> List[float]:
        if len(values) < 4:
            return values
        sorted_values = sorted(values)
        n = len(sorted_values)
        q1 = sorted_values[n // 4]
        q3 = sorted_values[(3 * n) // 4]
        iqr = q3 - q1
        lower_bound = q1 - 1.5 * iqr
        upper_bound = q3 + 1.5 * iqr
        return [v for v in values if lower_bound <= v <= upper_bound]

    @staticmethod
    def remove_outliers_std(values: List[float], std_threshold: float = None) -> List[float]:
        if len(values) < 3:
            return values
        threshold = std_threshold or settings.NOISE_REDUCTION_OUTLIER_STD_THRESHOLD
        mean = sum(values) / len(values)
        variance = sum((x - mean) ** 2 for x in values) / len(values)
        std = math.sqrt(variance)
        if std == 0:
            return values
        return [v for v in values if abs(v - mean) <= threshold * std]

    @classmethod
    def kalman_filter(cls, key: str, value: float) -> float:
        if key not in cls._kalman_filters:
            cls._kalman_filters[key] = KalmanFilter()
        return cls._kalman_filters[key].update(value)

    @classmethod
    def ema_smooth(cls, key: str, value: float, alpha: float = None) -> float:
        if key not in cls._ema_smoothers:
            cls._ema_smoothers[key] = EMASmoother(alpha)
        return cls._ema_smoothers[key].update(value)

    @classmethod
    def sliding_window_smooth(cls, key: str, value: float, window: int = 5) -> float:
        if key not in cls._sliding_windows:
            cls._sliding_windows[key] = SlidingWindowSmoother(window)
        return cls._sliding_windows[key].update(value)

    @classmethod
    def savitzky_golay_smooth(cls, key: str, value: float, window: int = 7, order: int = 2) -> float:
        if key not in cls._sg_smoothers:
            cls._sg_smoothers[key] = SavitzkyGolaySmoother(window, order)
        return cls._sg_smoothers[key].update(value)

    @classmethod
    def denoise_value(cls, key: str, value: float, method: str = 'ema') -> float:
        if not settings.NOISE_REDUCTION_ENABLED:
            return value
        try:
            if method == 'kalman':
                return cls.kalman_filter(key, value)
            elif method == 'median':
                return cls.sliding_window_smooth(key, value)
            elif method == 'savgol':
                return cls.savitzky_golay_smooth(key, value)
            else:
                return cls.ema_smooth(key, value)
        except Exception as e:
            logger.debug(f"降噪处理失败 key={key}: {e}")
            return value

    @staticmethod
    def multi_sample_average(sample_fn, count: int = None, interval: float = 0.05) -> float:
        import time
        n = count or settings.NOISE_REDUCTION_SAMPLE_COUNT
        samples = []
        for _ in range(n):
            try:
                val = sample_fn()
                if val is not None:
                    samples.append(val)
            except Exception:
                pass
            time.sleep(interval)
        if not samples:
            return 0.0
        return sum(samples) / len(samples)

    @staticmethod
    def compute_statistics(values: List[float]) -> Dict[str, float]:
        if not values:
            return {'count': 0, 'mean': 0, 'std': 0, 'min': 0, 'max': 0, 'p95': 0, 'p99': 0}
        n = len(values)
        mean = sum(values) / n
        variance = sum((x - mean) ** 2 for x in values) / n
        std = math.sqrt(variance)
        sorted_vals = sorted(values)
        return {
            'count': n,
            'mean': mean,
            'std': std,
            'min': sorted_vals[0],
            'max': sorted_vals[-1],
            'p95': sorted_vals[min(int(n * 0.95), n - 1)],
            'p99': sorted_vals[min(int(n * 0.99), n - 1)]
        }

    @staticmethod
    def validate_metrics_data(data: Dict[str, Any]) -> bool:
        required_fields = ['timestamp']
        for field in required_fields:
            if field not in data:
                logger.warning(f"指标数据缺少必要字段: {field}")
                return False
        return True

    @staticmethod
    def format_bytes(bytes_value: int) -> str:
        for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
            if abs(bytes_value) < 1024.0:
                return f"{bytes_value:.2f} {unit}"
            bytes_value /= 1024.0
        return f"{bytes_value:.2f} PB"

    @classmethod
    def reset_all_filters(cls):
        cls._kalman_filters.clear()
        cls._ema_smoothers.clear()
        cls._sliding_windows.clear()
        cls._sg_smoothers.clear()
        logger.info("所有降噪过滤器已重置")
