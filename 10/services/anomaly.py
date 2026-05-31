import logging
from datetime import datetime, timedelta
from typing import Dict, Any, Optional, List
from collections import deque

from config import settings, get_threshold_manager
from db.influxdb_client import get_influxdb_manager

logger = logging.getLogger(__name__)


class AnomalyDetector:
    def __init__(self):
        self.alert_history: Dict[str, datetime] = {}
        self._consecutive_exceed: Dict[str, deque] = {}
        self._tm = get_threshold_manager()

    @property
    def _required_consecutive(self) -> int:
        return self._tm.get_threshold('ALERT_CONSECUTIVE_COUNT', settings.ALERT_CONSECUTIVE_COUNT)

    def _should_trigger_alert(self, alert_key: str, is_exceeding: bool) -> bool:
        if alert_key not in self._consecutive_exceed:
            self._consecutive_exceed[alert_key] = deque(maxlen=self._required_consecutive)

        self._consecutive_exceed[alert_key].append(is_exceeding)

        if len(self._consecutive_exceed[alert_key]) < self._required_consecutive:
            return False

        return all(self._consecutive_exceed[alert_key])

    def check_cpu_anomaly(self, hostname: str, cpu_data: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        try:
            usage = cpu_data.get('usage_percent', 0)
            cpu_threshold = self._tm.get_threshold('CPU_USAGE_THRESHOLD', settings.CPU_USAGE_THRESHOLD)
            alert_key = f"{hostname}_cpu_usage_percent"
            is_exceeding = usage > cpu_threshold

            if is_exceeding and self._should_trigger_alert(alert_key, is_exceeding):
                return self._create_alert(
                    hostname=hostname,
                    metric_type='cpu',
                    metric_name='usage_percent',
                    threshold=cpu_threshold,
                    current_value=usage,
                    severity='critical' if usage > 95 else 'warning',
                    message=f"CPU使用率过高: {usage:.1f}%, 阈值: {cpu_threshold}%"
                )

            if not is_exceeding:
                self._consecutive_exceed[alert_key].clear()

            iowait = cpu_data.get('iowait_percent', 0)
            iowait_threshold = self._tm.get_threshold('CPU_IOWAIT_THRESHOLD', settings.CPU_IOWAIT_THRESHOLD)
            iowait_key = f"{hostname}_cpu_iowait"
            iowait_exceeding = iowait > iowait_threshold

            if iowait_exceeding and self._should_trigger_alert(iowait_key, iowait_exceeding):
                return self._create_alert(
                    hostname=hostname,
                    metric_type='cpu',
                    metric_name='iowait_percent',
                    threshold=iowait_threshold,
                    current_value=iowait,
                    severity='warning',
                    message=f"CPU IO等待过高: {iowait:.1f}%, 阈值: {iowait_threshold}%"
                )

            if not iowait_exceeding:
                self._consecutive_exceed[iowait_key].clear()

            load_avg_1m = cpu_data.get('load_avg_1m', 0)
            cpu_count = cpu_data.get('cpu_count_logical', 1)
            load_multiplier = self._tm.get_threshold('CPU_LOAD_THRESHOLD_MULTIPLIER', settings.CPU_LOAD_THRESHOLD_MULTIPLIER)
            load_threshold = cpu_count * load_multiplier
            load_alert_key = f"{hostname}_cpu_load_avg"
            load_exceeding = load_avg_1m > load_threshold

            if load_exceeding and self._should_trigger_alert(load_alert_key, load_exceeding):
                return self._create_alert(
                    hostname=hostname,
                    metric_type='cpu',
                    metric_name='load_avg',
                    threshold=load_threshold,
                    current_value=load_avg_1m,
                    severity='warning',
                    message=f"系统负载过高: {load_avg_1m:.2f}, 建议阈值: {load_threshold:.2f}"
                )

            if not load_exceeding:
                self._consecutive_exceed[load_alert_key].clear()

            return None
        except Exception as e:
            logger.error(f"CPU异常检测失败: {e}")
            return None

    def check_memory_anomaly(self, hostname: str, memory_data: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        try:
            usage_percent = memory_data.get('usage_percent', 0)
            mem_threshold = self._tm.get_threshold('MEMORY_USAGE_THRESHOLD', settings.MEMORY_USAGE_THRESHOLD)
            alert_key = f"{hostname}_memory_usage_percent"
            is_exceeding = usage_percent > mem_threshold

            if is_exceeding and self._should_trigger_alert(alert_key, is_exceeding):
                return self._create_alert(
                    hostname=hostname,
                    metric_type='memory',
                    metric_name='usage_percent',
                    threshold=mem_threshold,
                    current_value=usage_percent,
                    severity='critical' if usage_percent > 95 else 'warning',
                    message=f"内存使用率过高: {usage_percent:.1f}%, 阈值: {mem_threshold}%"
                )

            if not is_exceeding:
                self._consecutive_exceed[alert_key].clear()

            available_mb = memory_data.get('available', 0) / (1024 * 1024)
            avail_threshold = self._tm.get_threshold('MEMORY_AVAILABLE_THRESHOLD_MB', settings.MEMORY_AVAILABLE_THRESHOLD_MB)
            avail_key = f"{hostname}_memory_available"
            avail_exceeding = available_mb < avail_threshold

            if avail_exceeding and self._should_trigger_alert(avail_key, avail_exceeding):
                return self._create_alert(
                    hostname=hostname,
                    metric_type='memory',
                    metric_name='available',
                    threshold=avail_threshold,
                    current_value=available_mb,
                    severity='critical',
                    message=f"可用内存不足: {available_mb:.0f}MB, 阈值: {avail_threshold}MB"
                )

            if not avail_exceeding:
                self._consecutive_exceed[avail_key].clear()

            swap_percent = memory_data.get('swap_percent', 0)
            swap_threshold = self._tm.get_threshold('MEMORY_SWAP_THRESHOLD', settings.MEMORY_SWAP_THRESHOLD)
            swap_alert_key = f"{hostname}_memory_swap_percent"
            swap_exceeding = swap_percent > swap_threshold

            if swap_exceeding and self._should_trigger_alert(swap_alert_key, swap_exceeding):
                return self._create_alert(
                    hostname=hostname,
                    metric_type='memory',
                    metric_name='swap_percent',
                    threshold=swap_threshold,
                    current_value=swap_percent,
                    severity='warning',
                    message=f"交换分区使用率过高: {swap_percent:.1f}%"
                )

            if not swap_exceeding:
                self._consecutive_exceed[swap_alert_key].clear()

            return None
        except Exception as e:
            logger.error(f"内存异常检测失败: {e}")
            return None

    def check_disk_anomaly(self, hostname: str, disk_data: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        try:
            usage_percent = disk_data.get('usage_percent', 0)
            mount_point = disk_data.get('mount_point', 'unknown')
            device = disk_data.get('device', 'unknown')

            disk_threshold = self._tm.get_threshold('DISK_USAGE_THRESHOLD', settings.DISK_USAGE_THRESHOLD)
            alert_key = f"{hostname}_disk_{device}_usage_percent"
            is_exceeding = usage_percent > disk_threshold

            if is_exceeding and self._should_trigger_alert(alert_key, is_exceeding):
                return self._create_alert(
                    hostname=hostname,
                    metric_type='disk',
                    metric_name='usage_percent',
                    threshold=disk_threshold,
                    current_value=usage_percent,
                    severity='critical' if usage_percent > 95 else 'warning',
                    message=f"磁盘使用率过高: {device} ({mount_point}) - {usage_percent:.1f}%"
                )

            if not is_exceeding:
                self._consecutive_exceed[alert_key].clear()

            free_bytes = disk_data.get('free', 0)
            free_gb = free_bytes / (1024 ** 3)
            free_threshold_gb = self._tm.get_threshold('DISK_FREE_THRESHOLD_GB', settings.DISK_FREE_THRESHOLD_GB)
            free_alert_key = f"{hostname}_disk_{device}_free_space"
            free_exceeding = free_gb < free_threshold_gb

            if free_exceeding and self._should_trigger_alert(free_alert_key, free_exceeding):
                return self._create_alert(
                    hostname=hostname,
                    metric_type='disk',
                    metric_name='free_space',
                    threshold=free_threshold_gb * (1024 ** 3),
                    current_value=free_bytes,
                    severity='critical',
                    message=f"磁盘空间不足: {device} ({mount_point}) - 剩余: {free_gb:.2f}GB"
                )

            if not free_exceeding:
                self._consecutive_exceed[free_alert_key].clear()

            return None
        except Exception as e:
            logger.error(f"磁盘异常检测失败: {e}")
            return None

    def check_network_anomaly(self, hostname: str, network_data: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        try:
            interface = network_data.get('interface', 'unknown')

            errors_in_rate = network_data.get('errors_in_rate', 0)
            errors_out_rate = network_data.get('errors_out_rate', 0)
            drops_in_rate = network_data.get('drops_in_rate', 0)
            drops_out_rate = network_data.get('drops_out_rate', 0)

            total_errors_rate = errors_in_rate + errors_out_rate
            total_drops_rate = drops_in_rate + drops_out_rate

            error_threshold = self._tm.get_threshold('NETWORK_ERROR_RATE_THRESHOLD', settings.NETWORK_ERROR_RATE_THRESHOLD)
            error_alert_key = f"{hostname}_network_{interface}_errors_rate"
            error_exceeding = total_errors_rate > error_threshold

            if error_exceeding and self._should_trigger_alert(error_alert_key, error_exceeding):
                return self._create_alert(
                    hostname=hostname,
                    metric_type='network',
                    metric_name='errors_rate',
                    threshold=error_threshold,
                    current_value=total_errors_rate,
                    severity='warning',
                    message=f"网络接口错误速率过高: {interface} - {total_errors_rate:.2f} 次/秒"
                )

            if not error_exceeding:
                self._consecutive_exceed[error_alert_key].clear()

            drop_threshold = self._tm.get_threshold('NETWORK_DROP_RATE_THRESHOLD', settings.NETWORK_DROP_RATE_THRESHOLD)
            drop_alert_key = f"{hostname}_network_{interface}_drops_rate"
            drop_exceeding = total_drops_rate > drop_threshold

            if drop_exceeding and self._should_trigger_alert(drop_alert_key, drop_exceeding):
                return self._create_alert(
                    hostname=hostname,
                    metric_type='network',
                    metric_name='drops_rate',
                    threshold=drop_threshold,
                    current_value=total_drops_rate,
                    severity='warning',
                    message=f"网络接口丢包速率过高: {interface} - {total_drops_rate:.2f} 包/秒"
                )

            if not drop_exceeding:
                self._consecutive_exceed[drop_alert_key].clear()

            return None
        except Exception as e:
            logger.error(f"网络异常检测失败: {e}")
            return None

    def check_all_anomalies(self, metrics_data: Dict[str, Any]) -> List[Dict[str, Any]]:
        alerts = []
        hostname = metrics_data.get('hostname', 'unknown')

        if 'cpu' in metrics_data:
            cpu_alert = self.check_cpu_anomaly(hostname, metrics_data['cpu'])
            if cpu_alert:
                alerts.append(cpu_alert)

        if 'memory' in metrics_data:
            memory_alert = self.check_memory_anomaly(hostname, metrics_data['memory'])
            if memory_alert:
                alerts.append(memory_alert)

        if 'disks' in metrics_data:
            for disk in metrics_data['disks']:
                disk_alert = self.check_disk_anomaly(hostname, disk)
                if disk_alert:
                    alerts.append(disk_alert)

        if 'networks' in metrics_data:
            for network in metrics_data['networks']:
                network_alert = self.check_network_anomaly(hostname, network)
                if network_alert:
                    alerts.append(network_alert)

        return alerts

    def _create_alert(self, hostname: str, metric_type: str, metric_name: str,
                      threshold: float, current_value: float,
                      severity: str, message: str) -> Optional[Dict[str, Any]]:
        alert_key = f"{hostname}_{metric_type}_{metric_name}_{severity}"
        cooldown = self._tm.get_threshold('ALERT_COOLDOWN_SECONDS', settings.ALERT_COOLDOWN_SECONDS)

        if alert_key in self.alert_history:
            last_alert_time = self.alert_history[alert_key]
            if (datetime.utcnow() - last_alert_time).total_seconds() < cooldown:
                logger.debug(f"告警冷却中, 跳过告警: {alert_key}")
                return None

        alert = {
            'hostname': hostname,
            'metric_type': metric_type,
            'metric_name': metric_name,
            'threshold': threshold,
            'current_value': current_value,
            'severity': severity,
            'message': message,
            'timestamp': datetime.utcnow().isoformat()
        }

        self.alert_history[alert_key] = datetime.utcnow()
        logger.warning(f"检测到异常: {hostname} - {message}")
        return alert

    def reset_alert_history(self):
        self.alert_history.clear()
        self._consecutive_exceed.clear()
        logger.info("告警历史已重置")


anomaly_detector = AnomalyDetector()


def get_anomaly_detector() -> AnomalyDetector:
    return anomaly_detector
