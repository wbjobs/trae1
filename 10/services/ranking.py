import logging
from datetime import datetime, timedelta
from typing import Dict, Any, List, Optional

from config import settings
from db.influxdb_client import get_influxdb_manager

logger = logging.getLogger(__name__)


class RankingService:
    def __init__(self):
        self.db = get_influxdb_manager()
        self.top_n = settings.RANKING_TOP_N
        self.time_window = settings.RANKING_TIME_WINDOW_MINUTES

    def _get_time_range(self, minutes: int = None) -> tuple:
        window = minutes or self.time_window
        end_time = datetime.utcnow()
        start_time = end_time - timedelta(minutes=window)
        return start_time, end_time

    def _query_measurement_ranking(self, measurement: str, field: str,
                                   start_time: datetime, end_time: datetime,
                                   top_n: int = None,
                                   group_by: str = "hostname",
                                   aggregation: str = "mean") -> List[Dict]:
        try:
            n = top_n or self.top_n
            query = f'''
            from(bucket: "{self.db._client or 'server_metrics'}")
                |> range(start: time(v: "{start_time.isoformat()}Z"), stop: time(v: "{end_time.isoformat()}Z"))
                |> filter(fn: (r) => r["_measurement"] == "{measurement}" and r["_field"] == "{field}")
                |> group(columns: ["{group_by}"])
                |> {aggregation}()
                |> sort(columns: ["_value"], desc: true)
                |> limit(n: {n})
                |> yield(name: "ranking")
            '''
            results = self.db.query_metrics(query)
            rankings = []
            for r in results:
                rankings.append({
                    group_by: r.get(group_by, 'unknown'),
                    'value': r.get('_value', 0),
                    'metric': field,
                    'measurement': measurement
                })
            return rankings
        except Exception as e:
            logger.error(f"查询排行失败 {measurement}/{field}: {e}")
            return []

    def get_cpu_ranking(self, minutes: int = None, top_n: int = None) -> List[Dict]:
        start, end = self._get_time_range(minutes)
        return self._query_measurement_ranking(
            measurement="cpu_usage",
            field="usage_percent",
            start_time=start,
            end_time=end,
            top_n=top_n
        )

    def get_memory_ranking(self, minutes: int = None, top_n: int = None) -> List[Dict]:
        start, end = self._get_time_range(minutes)
        return self._query_measurement_ranking(
            measurement="memory_usage",
            field="usage_percent",
            start_time=start,
            end_time=end,
            top_n=top_n
        )

    def get_disk_ranking(self, minutes: int = None, top_n: int = None) -> List[Dict]:
        start, end = self._get_time_range(minutes)
        return self._query_measurement_ranking(
            measurement="disk_usage",
            field="usage_percent",
            start_time=start,
            end_time=end,
            top_n=top_n,
            group_by="device"
        )

    def get_disk_io_ranking(self, minutes: int = None, top_n: int = None) -> List[Dict]:
        start, end = self._get_time_range(minutes)
        read_ranking = self._query_measurement_ranking(
            measurement="disk_usage",
            field="read_bytes",
            start_time=start,
            end_time=end,
            top_n=top_n,
            group_by="device",
            aggregation="max"
        )
        write_ranking = self._query_measurement_ranking(
            measurement="disk_usage",
            field="write_bytes",
            start_time=start,
            end_time=end,
            top_n=top_n,
            group_by="device",
            aggregation="max"
        )
        combined = {}
        for r in read_ranking:
            key = r.get('device', 'unknown')
            if key not in combined:
                combined[key] = {'device': key, 'read_bytes': 0, 'write_bytes': 0}
            combined[key]['read_bytes'] = r.get('value', 0)
        for r in write_ranking:
            key = r.get('device', 'unknown')
            if key not in combined:
                combined[key] = {'device': key, 'read_bytes': 0, 'write_bytes': 0}
            combined[key]['write_bytes'] = r.get('value', 0)

        result = list(combined.values())
        result.sort(key=lambda x: x.get('read_bytes', 0) + x.get('write_bytes', 0), reverse=True)
        return result[:(top_n or self.top_n)]

    def get_network_ranking(self, minutes: int = None, top_n: int = None) -> List[Dict]:
        start, end = self._get_time_range(minutes)
        sent_ranking = self._query_measurement_ranking(
            measurement="network_usage",
            field="bytes_sent_rate",
            start_time=start,
            end_time=end,
            top_n=top_n,
            group_by="interface"
        )
        recv_ranking = self._query_measurement_ranking(
            measurement="network_usage",
            field="bytes_recv_rate",
            start_time=start,
            end_time=end,
            top_n=top_n,
            group_by="interface"
        )
        combined = {}
        for r in sent_ranking:
            key = r.get('interface', 'unknown')
            if key not in combined:
                combined[key] = {'interface': key, 'bytes_sent_rate': 0, 'bytes_recv_rate': 0}
            combined[key]['bytes_sent_rate'] = r.get('value', 0)
        for r in recv_ranking:
            key = r.get('interface', 'unknown')
            if key not in combined:
                combined[key] = {'interface': key, 'bytes_sent_rate': 0, 'bytes_recv_rate': 0}
            combined[key]['bytes_recv_rate'] = r.get('value', 0)

        result = list(combined.values())
        result.sort(key=lambda x: x.get('bytes_sent_rate', 0) + x.get('bytes_recv_rate', 0), reverse=True)
        return result[:(top_n or self.top_n)]

    def get_all_rankings(self, minutes: int = None, top_n: int = None) -> Dict[str, Any]:
        return {
            'cpu_ranking': self.get_cpu_ranking(minutes, top_n),
            'memory_ranking': self.get_memory_ranking(minutes, top_n),
            'disk_usage_ranking': self.get_disk_ranking(minutes, top_n),
            'disk_io_ranking': self.get_disk_io_ranking(minutes, top_n),
            'network_ranking': self.get_network_ranking(minutes, top_n),
            'time_window_minutes': minutes or self.time_window,
            'top_n': top_n or self.top_n,
            'generated_at': datetime.utcnow().isoformat()
        }

    def get_host_resource_summary(self, hostname: str, minutes: int = None) -> Dict[str, Any]:
        start, end = self._get_time_range(minutes)
        summary = {'hostname': hostname}

        try:
            cpu_result = self._query_measurement_ranking(
                measurement="cpu_usage",
                field="usage_percent",
                start_time=start,
                end_time=end,
                top_n=1
            )
            if cpu_result:
                summary['cpu_usage'] = cpu_result[0].get('value', 0)
        except Exception:
            summary['cpu_usage'] = 0

        try:
            mem_result = self._query_measurement_ranking(
                measurement="memory_usage",
                field="usage_percent",
                start_time=start,
                end_time=end,
                top_n=1
            )
            if mem_result:
                summary['memory_usage'] = mem_result[0].get('value', 0)
        except Exception:
            summary['memory_usage'] = 0

        return summary

    def get_overloaded_hosts(self, minutes: int = None,
                             cpu_threshold: float = None,
                             memory_threshold: float = None,
                             disk_threshold: float = None) -> List[Dict]:
        start, end = self._get_time_range(minutes)
        from config import get_threshold_manager
        tm = get_threshold_manager()

        cpu_th = cpu_threshold or tm.get_threshold('CPU_USAGE_THRESHOLD', 90.0)
        mem_th = memory_threshold or tm.get_threshold('MEMORY_USAGE_THRESHOLD', 90.0)
        disk_th = disk_threshold or tm.get_threshold('DISK_USAGE_THRESHOLD', 85.0)

        overloaded = []

        cpu_ranking = self.get_cpu_ranking(minutes)
        for host in cpu_ranking:
            if host.get('value', 0) > cpu_th:
                overloaded.append({
                    'hostname': host.get('hostname', 'unknown'),
                    'resource': 'cpu',
                    'usage': host.get('value', 0),
                    'threshold': cpu_th
                })

        mem_ranking = self.get_memory_ranking(minutes)
        for host in mem_ranking:
            if host.get('value', 0) > mem_th:
                overloaded.append({
                    'hostname': host.get('hostname', 'unknown'),
                    'resource': 'memory',
                    'usage': host.get('value', 0),
                    'threshold': mem_th
                })

        disk_ranking = self.get_disk_ranking(minutes)
        for host in disk_ranking:
            if host.get('value', 0) > disk_th:
                overloaded.append({
                    'hostname': host.get('device', 'unknown'),
                    'resource': 'disk',
                    'usage': host.get('value', 0),
                    'threshold': disk_th
                })

        return overloaded


ranking_service = RankingService()


def get_ranking_service() -> RankingService:
    return ranking_service
