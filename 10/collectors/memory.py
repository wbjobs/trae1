import logging
from typing import Dict, Any
from datetime import datetime
import psutil

logger = logging.getLogger(__name__)


class MemoryCollector:
    @staticmethod
    def collect() -> Dict[str, Any]:
        try:
            vm = psutil.virtual_memory()
            swap = psutil.swap_memory()

            data = {
                "total": vm.total,
                "available": vm.available,
                "used": vm.used,
                "free": vm.free,
                "usage_percent": vm.percent,
                "buffers": getattr(vm, 'buffers', 0),
                "cached": getattr(vm, 'cached', 0),
                "shared": getattr(vm, 'shared', 0),
                "active": getattr(vm, 'active', 0),
                "inactive": getattr(vm, 'inactive', 0),
                "swap_total": swap.total,
                "swap_used": swap.used,
                "swap_free": swap.free,
                "swap_percent": swap.percent,
                "timestamp": datetime.utcnow().isoformat()
            }

            logger.debug(f"内存指标采集成功: usage={vm.percent}%, used={vm.used // (1024**2)}MB")
            return data
        except Exception as e:
            logger.error(f"内存指标采集失败: {e}")
            raise
