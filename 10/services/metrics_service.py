import logging
from typing import Dict, Any, Optional, List

from collectors.collector import get_metrics_collector
from services.anomaly import get_anomaly_detector
from services.reporter import get_data_reporter
from services.filter import DataFilter

logger = logging.getLogger(__name__)


class MetricsService:
    def __init__(self):
        self.collector = get_metrics_collector()
        self.anomaly_detector = get_anomaly_detector()
        self.reporter = get_data_reporter()
        self.filter = DataFilter()

    def collect_and_report(self) -> Dict[str, Any]:
        try:
            metrics = self.collector.collect_all()

            if not self.filter.validate_metrics_data(metrics):
                logger.error("采集的指标数据验证失败")
                return {"success": False, "message": "数据验证失败"}

            hostname = metrics.get('hostname', 'unknown')
            alerts = self.anomaly_detector.check_all_anomalies(metrics)

            report_results = self.reporter.report_all_metrics(metrics)

            alert_results = []
            if alerts:
                for alert in alerts:
                    alert_hostname = alert.get('hostname', hostname)
                    alert_result = self.reporter.report_alert(alert_hostname, alert)
                    alert_results.append({
                        "alert": alert,
                        "reported": alert_result
                    })

            result = {
                "success": True,
                "message": "采集并上报成功",
                "hostname": hostname,
                "timestamp": metrics.get('timestamp'),
                "metrics_reported": report_results,
                "alerts": alert_results,
                "alert_count": len(alerts)
            }

            logger.info(f"指标采集上报完成: {hostname}, 告警数: {len(alerts)}")
            return result

        except Exception as e:
            logger.error(f"指标采集上报失败: {e}")
            return {"success": False, "message": str(e)}

    def collect_cpu(self) -> Dict[str, Any]:
        try:
            metrics = self.collector.collect_cpu()
            hostname = metrics.get('hostname', 'unknown')

            if 'cpu' in metrics:
                self.reporter.report_cpu_metrics(hostname, metrics['cpu'])
                alert = self.anomaly_detector.check_cpu_anomaly(hostname, metrics['cpu'])
                if alert:
                    self.reporter.report_alert(hostname, alert)
                    metrics['alerts'] = [alert]

            return metrics
        except Exception as e:
            logger.error(f"CPU采集失败: {e}")
            raise

    def collect_memory(self) -> Dict[str, Any]:
        try:
            metrics = self.collector.collect_memory()
            hostname = metrics.get('hostname', 'unknown')

            if 'memory' in metrics:
                self.reporter.report_memory_metrics(hostname, metrics['memory'])
                alert = self.anomaly_detector.check_memory_anomaly(hostname, metrics['memory'])
                if alert:
                    self.reporter.report_alert(hostname, alert)
                    metrics['alerts'] = [alert]

            return metrics
        except Exception as e:
            logger.error(f"内存采集失败: {e}")
            raise

    def collect_disk(self) -> Dict[str, Any]:
        try:
            metrics = self.collector.collect_disk()
            hostname = metrics.get('hostname', 'unknown')

            if 'disks' in metrics:
                self.reporter.report_disk_metrics(hostname, metrics['disks'])
                alerts = []
                for disk in metrics['disks']:
                    alert = self.anomaly_detector.check_disk_anomaly(hostname, disk)
                    if alert:
                        alerts.append(alert)
                        self.reporter.report_alert(hostname, alert)
                if alerts:
                    metrics['alerts'] = alerts

            return metrics
        except Exception as e:
            logger.error(f"磁盘采集失败: {e}")
            raise

    def collect_network(self) -> Dict[str, Any]:
        try:
            metrics = self.collector.collect_network()
            hostname = metrics.get('hostname', 'unknown')

            if 'networks' in metrics:
                self.reporter.report_network_metrics(hostname, metrics['networks'])
                alerts = []
                for network in metrics['networks']:
                    alert = self.anomaly_detector.check_network_anomaly(hostname, network)
                    if alert:
                        alerts.append(alert)
                        self.reporter.report_alert(hostname, alert)
                if alerts:
                    metrics['alerts'] = alerts

            return metrics
        except Exception as e:
            logger.error(f"网络采集失败: {e}")
            raise


metrics_service = MetricsService()


def get_metrics_service() -> MetricsService:
    return metrics_service
