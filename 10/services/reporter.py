import logging
from datetime import datetime
from typing import Dict, Any, List, Optional

from influxdb_client import Point

from db.influxdb_client import get_influxdb_manager
from services.filter import DataFilter

logger = logging.getLogger(__name__)


class DataReporter:
    def __init__(self):
        self.influxdb = get_influxdb_manager()
        self.filter = DataFilter()

    def _safe_float(self, value: Any, default: float = 0.0) -> float:
        if value is None:
            return default
        try:
            return float(value)
        except (ValueError, TypeError):
            return default

    def _safe_int(self, value: Any, default: int = 0) -> int:
        if value is None:
            return default
        try:
            return int(value)
        except (ValueError, TypeError):
            return default

    def _clean_fields(self, fields: Dict[str, Any]) -> Dict[str, Any]:
        return {k: v for k, v in fields.items() if v is not None}

    def report_cpu_metrics(self, hostname: str, cpu_data: Dict[str, Any]) -> bool:
        try:
            measurement = "cpu_usage"
            tags = {"hostname": hostname}
            fields = {
                "usage_percent": self._safe_float(cpu_data.get('usage_percent')),
                "user_percent": self._safe_float(cpu_data.get('user_percent')),
                "system_percent": self._safe_float(cpu_data.get('system_percent')),
                "idle_percent": self._safe_float(cpu_data.get('idle_percent')),
                "iowait_percent": self._safe_float(cpu_data.get('iowait_percent')),
                "load_avg_1m": self._safe_float(cpu_data.get('load_avg_1m')),
                "load_avg_5m": self._safe_float(cpu_data.get('load_avg_5m')),
                "load_avg_15m": self._safe_float(cpu_data.get('load_avg_15m'))
            }
            fields = self._clean_fields(fields)

            if not fields:
                logger.warning(f"CPU指标字段为空, 跳过上报: {hostname}")
                return False

            self.influxdb.write_metrics(
                measurement=measurement,
                tags=tags,
                fields=fields
            )
            logger.info(f"CPU指标上报成功: {hostname}")
            return True
        except Exception as e:
            logger.error(f"CPU指标上报失败: {e}")
            return False

    def report_memory_metrics(self, hostname: str, memory_data: Dict[str, Any]) -> bool:
        try:
            measurement = "memory_usage"
            tags = {"hostname": hostname}
            fields = {
                "total": self._safe_int(memory_data.get('total')),
                "available": self._safe_int(memory_data.get('available')),
                "used": self._safe_int(memory_data.get('used')),
                "free": self._safe_int(memory_data.get('free')),
                "usage_percent": self._safe_float(memory_data.get('usage_percent')),
                "buffers": self._safe_int(memory_data.get('buffers')),
                "cached": self._safe_int(memory_data.get('cached')),
                "shared": self._safe_int(memory_data.get('shared')),
                "swap_total": self._safe_int(memory_data.get('swap_total')),
                "swap_used": self._safe_int(memory_data.get('swap_used')),
                "swap_percent": self._safe_float(memory_data.get('swap_percent'))
            }
            fields = self._clean_fields(fields)

            if not fields:
                logger.warning(f"内存指标字段为空, 跳过上报: {hostname}")
                return False

            self.influxdb.write_metrics(
                measurement=measurement,
                tags=tags,
                fields=fields
            )
            logger.info(f"内存指标上报成功: {hostname}")
            return True
        except Exception as e:
            logger.error(f"内存指标上报失败: {e}")
            return False

    def report_disk_metrics(self, hostname: str, disks_data: List[Dict[str, Any]]) -> bool:
        try:
            success_count = 0
            total_count = len(disks_data)

            for disk in disks_data:
                try:
                    measurement = "disk_usage"
                    tags = {
                        "hostname": hostname,
                        "device": disk.get('device', 'unknown'),
                        "mount_point": disk.get('mount_point', 'unknown')
                    }
                    fields = {
                        "total": self._safe_int(disk.get('total')),
                        "used": self._safe_int(disk.get('used')),
                        "free": self._safe_int(disk.get('free')),
                        "usage_percent": self._safe_float(disk.get('usage_percent')),
                        "read_bytes": self._safe_int(disk.get('read_bytes')),
                        "write_bytes": self._safe_int(disk.get('write_bytes')),
                        "read_count": self._safe_int(disk.get('read_count')),
                        "write_count": self._safe_int(disk.get('write_count'))
                    }
                    fields = self._clean_fields(fields)

                    if not fields:
                        logger.warning(f"磁盘指标字段为空, 跳过: {hostname}/{disk.get('device', 'unknown')}")
                        continue

                    self.influxdb.write_metrics(
                        measurement=measurement,
                        tags=tags,
                        fields=fields
                    )
                    success_count += 1
                except Exception as disk_e:
                    logger.warning(f"单磁盘上报失败 {hostname}/{disk.get('device', 'unknown')}: {disk_e}")
                    continue

            logger.info(f"磁盘指标上报成功: {hostname}, 成功: {success_count}/{total_count}")
            return success_count > 0
        except Exception as e:
            logger.error(f"磁盘指标上报失败: {e}")
            return False

    def report_network_metrics(self, hostname: str, networks_data: List[Dict[str, Any]]) -> bool:
        try:
            success_count = 0
            total_count = len(networks_data)

            for network in networks_data:
                try:
                    measurement = "network_usage"
                    tags = {
                        "hostname": hostname,
                        "interface": network.get('interface', 'unknown')
                    }
                    fields = {
                        "bytes_sent": self._safe_int(network.get('bytes_sent')),
                        "bytes_recv": self._safe_int(network.get('bytes_recv')),
                        "bytes_sent_rate": self._safe_float(network.get('bytes_sent_rate')),
                        "bytes_recv_rate": self._safe_float(network.get('bytes_recv_rate')),
                        "packets_sent": self._safe_int(network.get('packets_sent')),
                        "packets_recv": self._safe_int(network.get('packets_recv')),
                        "packets_sent_rate": self._safe_float(network.get('packets_sent_rate')),
                        "packets_recv_rate": self._safe_float(network.get('packets_recv_rate')),
                        "errors_in": self._safe_int(network.get('errors_in')),
                        "errors_out": self._safe_int(network.get('errors_out')),
                        "errors_in_rate": self._safe_float(network.get('errors_in_rate')),
                        "errors_out_rate": self._safe_float(network.get('errors_out_rate')),
                        "drops_in": self._safe_int(network.get('drops_in')),
                        "drops_out": self._safe_int(network.get('drops_out')),
                        "drops_in_rate": self._safe_float(network.get('drops_in_rate')),
                        "drops_out_rate": self._safe_float(network.get('drops_out_rate'))
                    }
                    fields = self._clean_fields(fields)

                    if not fields:
                        logger.warning(f"网络指标字段为空, 跳过: {hostname}/{network.get('interface', 'unknown')}")
                        continue

                    self.influxdb.write_metrics(
                        measurement=measurement,
                        tags=tags,
                        fields=fields
                    )
                    success_count += 1
                except Exception as net_e:
                    logger.warning(f"单网络接口上报失败 {hostname}/{network.get('interface', 'unknown')}: {net_e}")
                    continue

            logger.info(f"网络指标上报成功: {hostname}, 成功: {success_count}/{total_count}")
            return success_count > 0
        except Exception as e:
            logger.error(f"网络指标上报失败: {e}")
            return False

    def report_all_metrics(self, metrics_data: Dict[str, Any]) -> Dict[str, bool]:
        hostname = metrics_data.get('hostname', 'unknown')
        results = {}

        if 'cpu' in metrics_data:
            results['cpu'] = self.report_cpu_metrics(hostname, metrics_data['cpu'])

        if 'memory' in metrics_data:
            results['memory'] = self.report_memory_metrics(hostname, metrics_data['memory'])

        if 'disks' in metrics_data:
            results['disk'] = self.report_disk_metrics(hostname, metrics_data['disks'])

        if 'networks' in metrics_data:
            results['network'] = self.report_network_metrics(hostname, metrics_data['networks'])

        return results

    def report_alert(self, hostname: str, alert_data: Dict[str, Any]) -> bool:
        try:
            alert_record = {
                "hostname": hostname,
                "metric_type": alert_data.get('metric_type', 'unknown'),
                "metric_name": alert_data.get('metric_name', 'unknown'),
                "threshold": self._safe_float(alert_data.get('threshold')),
                "current_value": self._safe_float(alert_data.get('current_value')),
                "severity": alert_data.get('severity', 'warning'),
                "message": alert_data.get('message', ''),
                "status": "active"
            }
            alert_record = {k: v for k, v in alert_record.items() if v is not None}
            self.influxdb.write_alert(alert_record)
            logger.info(f"告警上报成功: {hostname} - {alert_data.get('message', '')}")
            return True
        except Exception as e:
            logger.error(f"告警上报失败: {e}")
            return False

    def report_batch_alerts(self, hostname: str, alerts: List[Dict[str, Any]]) -> int:
        success_count = 0
        for alert in alerts:
            if self.report_alert(hostname, alert):
                success_count += 1
        return success_count


data_reporter = DataReporter()


def get_data_reporter() -> DataReporter:
    return data_reporter
