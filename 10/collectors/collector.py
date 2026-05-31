import logging
import socket
from typing import Dict, Any
from datetime import datetime

from collectors.cpu import CPUCollector
from collectors.memory import MemoryCollector
from collectors.disk import DiskCollector
from collectors.network import NetworkCollector
from services.filter import DataFilter
from config import settings

logger = logging.getLogger(__name__)


class MetricsCollector:
    def __init__(self):
        self.cpu_collector = CPUCollector()
        self.memory_collector = MemoryCollector()
        self.disk_collector = DiskCollector()
        self.network_collector = NetworkCollector()
        self.filter = DataFilter()
        self.hostname = socket.gethostname()
        self.ip_address = self._get_ip_address()

    @staticmethod
    def _get_ip_address() -> str:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except Exception:
            return "127.0.0.1"

    def _apply_denoise(self, data: Dict[str, Any], prefix: str, fields: list) -> Dict[str, Any]:
        if not settings.NOISE_REDUCTION_ENABLED:
            return data
        for field in fields:
            if field in data and isinstance(data[field], (int, float)):
                key = f"{prefix}_{field}"
                data[field] = self.filter.denoise_value(key, data[field], method='ema')
        return data

    def collect_all(self) -> Dict[str, Any]:
        try:
            cpu_data = self.cpu_collector.collect()
            memory_data = self.memory_collector.collect()
            disks_data = self.disk_collector.collect()
            networks_data = self.network_collector.collect()

            cpu_data = self._apply_denoise(
                cpu_data, f"{self.hostname}_cpu",
                ['usage_percent', 'user_percent', 'system_percent', 'idle_percent', 'iowait_percent']
            )
            memory_data = self._apply_denoise(
                memory_data, f"{self.hostname}_memory",
                ['usage_percent', 'swap_percent']
            )

            for i, disk in enumerate(disks_data):
                disks_data[i] = self._apply_denoise(
                    disk, f"{self.hostname}_disk_{disk.get('device', i)}",
                    ['usage_percent']
                )

            for i, network in enumerate(networks_data):
                networks_data[i] = self._apply_denoise(
                    network, f"{self.hostname}_net_{network.get('interface', i)}",
                    ['bytes_sent_rate', 'bytes_recv_rate', 'errors_in_rate', 'errors_out_rate',
                     'drops_in_rate', 'drops_out_rate']
                )

            metrics = {
                "hostname": self.hostname,
                "ip_address": self.ip_address,
                "cpu": cpu_data,
                "memory": memory_data,
                "disks": disks_data,
                "networks": networks_data,
                "timestamp": datetime.utcnow().isoformat()
            }

            logger.info(f"全量指标采集成功: hostname={self.hostname}")
            return metrics
        except Exception as e:
            logger.error(f"全量指标采集失败: {e}")
            raise

    def collect_cpu(self) -> Dict[str, Any]:
        data = self.cpu_collector.collect()
        data = self._apply_denoise(
            data, f"{self.hostname}_cpu",
            ['usage_percent', 'user_percent', 'system_percent', 'idle_percent', 'iowait_percent']
        )
        return {
            "hostname": self.hostname,
            "ip_address": self.ip_address,
            "cpu": data,
            "timestamp": datetime.utcnow().isoformat()
        }

    def collect_memory(self) -> Dict[str, Any]:
        data = self.memory_collector.collect()
        data = self._apply_denoise(
            data, f"{self.hostname}_memory",
            ['usage_percent', 'swap_percent']
        )
        return {
            "hostname": self.hostname,
            "ip_address": self.ip_address,
            "memory": data,
            "timestamp": datetime.utcnow().isoformat()
        }

    def collect_disk(self) -> Dict[str, Any]:
        data = self.disk_collector.collect()
        for i, disk in enumerate(data):
            data[i] = self._apply_denoise(
                disk, f"{self.hostname}_disk_{disk.get('device', i)}",
                ['usage_percent']
            )
        return {
            "hostname": self.hostname,
            "ip_address": self.ip_address,
            "disks": data,
            "timestamp": datetime.utcnow().isoformat()
        }

    def collect_network(self) -> Dict[str, Any]:
        data = self.network_collector.collect()
        for i, network in enumerate(data):
            data[i] = self._apply_denoise(
                network, f"{self.hostname}_net_{network.get('interface', i)}",
                ['bytes_sent_rate', 'bytes_recv_rate', 'errors_in_rate', 'errors_out_rate',
                 'drops_in_rate', 'drops_out_rate']
            )
        return {
            "hostname": self.hostname,
            "ip_address": self.ip_address,
            "networks": data,
            "timestamp": datetime.utcnow().isoformat()
        }


metrics_collector = MetricsCollector()


def get_metrics_collector() -> MetricsCollector:
    return metrics_collector
