from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    APP_NAME: str = "Server Metrics Collector API"
    APP_VERSION: str = "1.1.0"
    HOST: str = "0.0.0.0"
    PORT: int = 8000
    DEBUG: bool = True

    INFLUXDB_URL: str = "http://localhost:8086"
    INFLUXDB_TOKEN: str = "your-influxdb-token"
    INFLUXDB_ORG: str = "your-org"
    INFLUXDB_BUCKET: str = "server_metrics"
    INFLUXDB_TIMEOUT: int = 30000

    COLLECT_INTERVAL: int = 5
    METRICS_RETENTION_DAYS: int = 30

    CPU_USAGE_THRESHOLD: float = 90.0
    CPU_LOAD_THRESHOLD_MULTIPLIER: float = 2.0
    CPU_IOWAIT_THRESHOLD: float = 50.0

    MEMORY_USAGE_THRESHOLD: float = 90.0
    MEMORY_SWAP_THRESHOLD: float = 50.0
    MEMORY_AVAILABLE_THRESHOLD_MB: int = 512

    DISK_USAGE_THRESHOLD: float = 85.0
    DISK_FREE_THRESHOLD_GB: float = 1.0
    DISK_IO_UTIL_THRESHOLD: float = 90.0

    NETWORK_THROUGHPUT_THRESHOLD: float = 100.0
    NETWORK_ERROR_RATE_THRESHOLD: float = 10.0
    NETWORK_DROP_RATE_THRESHOLD: float = 50.0

    ALERT_COOLDOWN_SECONDS: int = 300
    ALERT_CONSECUTIVE_COUNT: int = 3

    NOISE_REDUCTION_ENABLED: bool = True
    NOISE_REDUCTION_EMA_ALPHA: float = 0.3
    NOISE_REDUCTION_SAMPLE_COUNT: int = 3
    NOISE_REDUCTION_OUTLIER_STD_THRESHOLD: float = 3.0

    KALMAN_PROCESS_NOISE: float = 0.01
    KALMAN_MEASUREMENT_NOISE: float = 0.1

    MAX_CONCURRENT_COLLECTIONS: int = 10
    REMOTE_COLLECT_TIMEOUT: int = 5

    RANKING_TOP_N: int = 20
    RANKING_TIME_WINDOW_MINUTES: int = 5

    class Config:
        env_file = ".env"


settings = Settings()


class ThresholdManager:
    _instance = None
    _overrides: dict = {}

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance

    def get_threshold(self, name: str, default: float = None) -> float:
        if name in self._overrides:
            return self._overrides[name]
        return getattr(settings, name, default)

    def set_threshold(self, name: str, value: float) -> bool:
        if hasattr(settings, name) or name.startswith(('CPU_', 'MEMORY_', 'DISK_', 'NETWORK_', 'ALERT_')):
            self._overrides[name] = value
            return True
        return False

    def get_all_thresholds(self) -> dict:
        base = {}
        for attr in dir(settings):
            if attr.endswith('_THRESHOLD') or attr.endswith('_MULTIPLIER') or attr.endswith('_COUNT'):
                base[attr] = getattr(settings, attr)
        base.update(self._overrides)
        return base

    def reset_threshold(self, name: str) -> bool:
        if name in self._overrides:
            del self._overrides[name]
            return True
        return False

    def reset_all(self):
        self._overrides.clear()


threshold_manager = ThresholdManager()


def get_threshold_manager() -> ThresholdManager:
    return threshold_manager
