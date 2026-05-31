import logging
import os
import time
from typing import Dict, Any, Optional
from datetime import datetime
import psutil

logger = logging.getLogger(__name__)


class CPUCollector:
    _last_cpu_times = None
    _last_cpu_percent_times = None
    _last_sample_time: float = 0

    @staticmethod
    def collect() -> Dict[str, Any]:
        try:
            cpu_percent = CPUCollector._safe_cpu_percent()
            cpu_times_percent = CPUCollector._safe_cpu_times_percent()

            load_avg = (0.0, 0.0, 0.0)
            try:
                load_avg = os.getloadavg()
            except (OSError, AttributeError):
                pass

            cpu_count = psutil.cpu_count(logical=True)
            cpu_count_physical = psutil.cpu_count(logical=False)

            data = {
                "usage_percent": cpu_percent,
                "user_percent": cpu_times_percent.get('user', 0.0),
                "system_percent": cpu_times_percent.get('system', 0.0),
                "idle_percent": cpu_times_percent.get('idle', 0.0),
                "iowait_percent": cpu_times_percent.get('iowait', 0.0),
                "load_avg_1m": load_avg[0],
                "load_avg_5m": load_avg[1],
                "load_avg_15m": load_avg[2],
                "cpu_count_logical": cpu_count,
                "cpu_count_physical": cpu_count_physical or cpu_count,
                "timestamp": datetime.utcnow().isoformat()
            }

            logger.debug(f"CPU指标采集成功: usage={cpu_percent}%")
            return data
        except Exception as e:
            logger.error(f"CPU指标采集失败: {e}")
            raise

    @staticmethod
    def _safe_cpu_percent() -> float:
        try:
            current_cpu_times = psutil.cpu_times()
            current_time = time.time()

            if CPUCollector._last_cpu_times is None or CPUCollector._last_sample_time == 0:
                CPUCollector._last_cpu_times = current_cpu_times
                CPUCollector._last_sample_time = current_time
                psutil.cpu_percent(interval=None)
                return 0.0

            elapsed = current_time - CPUCollector._last_sample_time
            if elapsed < 0.1:
                return 0.0

            last_times = CPUCollector._last_cpu_times
            total_delta = sum(
                getattr(current_cpu_times, attr, 0) - getattr(last_times, attr, 0)
                for attr in ['user', 'system', 'idle', 'iowait']
                if hasattr(current_cpu_times, attr)
            )
            idle_delta = getattr(current_cpu_times, 'idle', 0) - getattr(last_times, 'idle', 0)

            if total_delta > 0:
                cpu_percent = (1.0 - idle_delta / total_delta) * 100.0
                cpu_percent = max(0.0, min(100.0, cpu_percent))
            else:
                cpu_percent = 0.0

            CPUCollector._last_cpu_times = current_cpu_times
            CPUCollector._last_sample_time = current_time
            return round(cpu_percent, 2)
        except Exception as e:
            logger.debug(f"CPU百分比计算异常: {e}, 回退到psutil.cpu_percent()")
            try:
                return psutil.cpu_percent(interval=None)
            except Exception:
                return 0.0

    @staticmethod
    def _safe_cpu_times_percent() -> Dict[str, float]:
        try:
            result = {'user': 0.0, 'system': 0.0, 'idle': 0.0, 'iowait': 0.0}
            current_times = psutil.cpu_times()
            current_time = time.time()

            if CPUCollector._last_cpu_percent_times is None:
                CPUCollector._last_cpu_percent_times = current_times
                return result

            elapsed = current_time - CPUCollector._last_sample_time
            if elapsed < 0.1:
                return result

            last_times = CPUCollector._last_cpu_percent_times
            cpu_count = psutil.cpu_count(logical=True) or 1

            for attr in ['user', 'system', 'idle', 'iowait']:
                current_val = getattr(current_times, attr, 0)
                last_val = getattr(last_times, attr, 0)
                delta = current_val - last_val
                if delta > 0 and elapsed > 0:
                    result[attr] = round(min(100.0, (delta / elapsed) / cpu_count * 100.0), 2)

            CPUCollector._last_cpu_percent_times = current_times
            return result
        except Exception as e:
            logger.debug(f"CPU时间百分比计算异常: {e}")
            return {'user': 0.0, 'system': 0.0, 'idle': 0.0, 'iowait': 0.0}

    @staticmethod
    def collect_per_cpu() -> Optional[Dict[str, Any]]:
        try:
            cpu_percents = psutil.cpu_percent(interval=None, percpu=True)
            data = {
                "per_cpu_usage": cpu_percents,
                "timestamp": datetime.utcnow().isoformat()
            }
            logger.debug(f"逐CPU指标采集成功, CPU数量: {len(cpu_percents)}")
            return data
        except Exception as e:
            logger.error(f"逐CPU指标采集失败: {e}")
            return None

    @classmethod
    def reset_cache(cls):
        cls._last_cpu_times = None
        cls._last_cpu_percent_times = None
        cls._last_sample_time = 0
