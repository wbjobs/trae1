import logging
from typing import List, Dict, Any
from datetime import datetime
import psutil

logger = logging.getLogger(__name__)


class DiskCollector:
    @staticmethod
    def collect() -> List[Dict[str, Any]]:
        try:
            disks = []
            partitions = psutil.disk_partitions(all=False)

            disk_io = psutil.disk_io_counters(perdisk=True)

            for partition in partitions:
                try:
                    usage = psutil.disk_usage(partition.mountpoint)
                    device_name = partition.device.split("/")[-1] if "/" in partition.device else partition.device

                    io_data = {
                        "read_bytes": 0,
                        "write_bytes": 0,
                        "read_count": 0,
                        "write_count": 0,
                        "read_time": 0,
                        "write_time": 0
                    }

                    if device_name in disk_io:
                        io = disk_io[device_name]
                        io_data = {
                            "read_bytes": io.read_bytes,
                            "write_bytes": io.write_bytes,
                            "read_count": io.read_count,
                            "write_count": io.write_count,
                            "read_time": getattr(io, 'read_time', 0),
                            "write_time": getattr(io, 'write_time', 0)
                        }

                    disk_data = {
                        "device": partition.device,
                        "mount_point": partition.mountpoint,
                        "fstype": partition.fstype,
                        "total": usage.total,
                        "used": usage.used,
                        "free": usage.free,
                        "usage_percent": usage.percent,
                        **io_data,
                        "timestamp": datetime.utcnow().isoformat()
                    }
                    disks.append(disk_data)
                except (PermissionError, OSError) as e:
                    logger.warning(f"无法访问磁盘分区 {partition.mountpoint}: {e}")
                    continue

            logger.debug(f"磁盘指标采集成功, 分区数量: {len(disks)}")
            return disks
        except Exception as e:
            logger.error(f"磁盘指标采集失败: {e}")
            raise

    @staticmethod
    def collect_disk_io() -> Dict[str, Any]:
        try:
            disk_io = psutil.disk_io_counters(perdisk=True)
            result = {}
            for device, io in disk_io.items():
                result[device] = {
                    "read_bytes": io.read_bytes,
                    "write_bytes": io.write_bytes,
                    "read_count": io.read_count,
                    "write_count": io.write_count,
                    "timestamp": datetime.utcnow().isoformat()
                }
            return result
        except Exception as e:
            logger.error(f"磁盘IO指标采集失败: {e}")
            return {}
