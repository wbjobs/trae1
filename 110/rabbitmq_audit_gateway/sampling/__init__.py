"""Message sampling mechanism"""
import random
import hashlib
import time
from dataclasses import dataclass
from typing import Dict, Optional, Callable
from enum import Enum


class SamplingStrategy(Enum):
    RANDOM = "random"
    HASH_BASED = "hash_based"
    TYPE_BASED = "type_based"
    RATE_LIMITED = "rate_limited"


@dataclass
class SamplingConfig:
    default_rate: float = 0.01
    large_message_threshold: int = 1048576  # 1MB
    large_message_rate: float = 0.001
    by_message_type: Dict[str, float] = None

    def __post_init__(self):
        if self.by_message_type is None:
            self.by_message_type = {
                "critical": 1.0,
                "normal": 0.1,
                "low": 0.01
            }


class SamplingResult:
    def __init__(self, should_sample: bool, reason: str, rate: float):
        self.should_sample = should_sample
        self.reason = reason
        self.rate = rate

    def __bool__(self):
        return self.should_sample


class MessageSampler:
    def __init__(self, config: SamplingConfig):
        self.config = config
        self._default_rate = config.default_rate
        self._type_rates = config.by_message_type
        self._strategy = SamplingStrategy.RANDOM
        self._callbacks: Dict[str, Callable] = {}
        self._rate_limit_count = 0
        self._rate_limit_window = 60
        self._rate_limit_max = 100
        self._window_start = time.time()
        # 新增大消息配置
        self._large_message_threshold = config.large_message_threshold
        self._large_message_rate = config.large_message_rate

    def set_strategy(self, strategy: SamplingStrategy) -> None:
        self._strategy = strategy

    def register_callback(self, message_type: str, callback: Callable) -> None:
        self._callbacks[message_type] = callback

    def should_sample_random(self, message_id: str, rate: Optional[float] = None) -> SamplingResult:
        """随机采样，支持指定采样率"""
        if rate is None:
            rate = self._default_rate
        should_sample = random.random() < rate
        return SamplingResult(should_sample, f"random_{rate}", rate)

    def should_sample_hash_based(self, message_id: str, rate: Optional[float] = None) -> SamplingResult:
        """基于哈希的采样，支持指定采样率"""
        hash_value = hashlib.md5(f"{message_id}_{time.time() // 60}".encode()).hexdigest()
        hash_int = int(hash_value[:8], 16)
        if rate is None:
            rate = self._default_rate
        threshold = int(rate * 0xFFFFFFFF)
        should_sample = hash_int < threshold
        return SamplingResult(should_sample, f"hash_{rate}", rate)

    def should_sample_type_based(self, message_id: str, message_type: str, rate: Optional[float] = None) -> SamplingResult:
        """基于类型的采样，支持指定采样率"""
        if rate is None:
            rate = self._type_rates.get(message_type, self._default_rate)
        should_sample = random.random() < rate
        reason = f"type_{message_type}_{rate}"
        return SamplingResult(should_sample, reason, rate)

    def should_sample_rate_limited(self, message_id: str) -> SamplingResult:
        current_time = time.time()

        if current_time - self._window_start >= self._rate_limit_window:
            self._rate_limit_count = 0
            self._window_start = current_time

        if self._rate_limit_count >= self._rate_limit_max:
            return SamplingResult(False, "rate_limited", 0.0)

        self._rate_limit_count += 1
        rate = min(1.0, self._rate_limit_max / max(1, self._rate_limit_count))
        return SamplingResult(True, "rate_limited", rate)

    def is_large_message(self, body_size: int) -> bool:
        """判断是否是大消息"""
        return body_size > self._large_message_threshold

    def should_sample(
        self,
        message_id: str,
        message_type: Optional[str] = None,
        strategy: Optional[SamplingStrategy] = None,
        body_size: Optional[int] = None
    ) -> SamplingResult:
        """判断是否应该采样，支持对大消息自动降采样"""
        if strategy is None:
            strategy = self._strategy

        # 对大消息使用降低的采样率
        if body_size is not None and self.is_large_message(body_size):
            if strategy == SamplingStrategy.RANDOM:
                return self.should_sample_random(message_id, self._large_message_rate)
            elif strategy == SamplingStrategy.HASH_BASED:
                return self.should_sample_hash_based(message_id, self._large_message_rate)
            elif strategy == SamplingStrategy.TYPE_BASED and message_type:
                return self.should_sample_type_based(message_id, message_type, self._large_message_rate)
            else:
                return self.should_sample_random(message_id, self._large_message_rate)

        # 正常消息的采样逻辑
        if message_type and message_type in self._type_rates:
            return self.should_sample_type_based(message_id, message_type)

        if strategy == SamplingStrategy.RANDOM:
            return self.should_sample_random(message_id)
        elif strategy == SamplingStrategy.HASH_BASED:
            return self.should_sample_hash_based(message_id)
        elif strategy == SamplingStrategy.TYPE_BASED and message_type:
            return self.should_sample_type_based(message_id, message_type)
        elif strategy == SamplingStrategy.RATE_LIMITED:
            return self.should_sample_rate_limited(message_id)
        else:
            return self.should_sample_random(message_id)

    def set_rate(self, rate: float) -> None:
        self._default_rate = max(0.0, min(1.0, rate))

    def set_type_rate(self, message_type: str, rate: float) -> None:
        self._type_rates[message_type] = max(0.0, min(1.0, rate))

    def set_rate_limit(self, max_samples: int, window_seconds: int) -> None:
        self._rate_limit_max = max_samples
        self._rate_limit_window = window_seconds

    def get_current_rate(self, message_type: Optional[str] = None) -> float:
        if message_type and message_type in self._type_rates:
            return self._type_rates[message_type]
        return self._default_rate

    def get_stats(self) -> Dict:
        return {
            "default_rate": self._default_rate,
            "type_rates": dict(self._type_rates),
            "strategy": self._strategy.value,
            "large_message_threshold": self._large_message_threshold,
            "large_message_rate": self._large_message_rate,
            "rate_limit": {
                "current_count": self._rate_limit_count,
                "max": self._rate_limit_max,
                "window_seconds": self._rate_limit_window
            }
        }
