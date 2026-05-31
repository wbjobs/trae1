import logging
from datetime import datetime, timedelta
from typing import List, Dict, Optional, Any
from influxdb_client import InfluxDBClient, Point, WriteOptions
from influxdb_client.client.write_api import SYNCHRONOUS
from config import settings

logger = logging.getLogger(__name__)


class InfluxDBManager:
    _instance = None
    _client = None
    _write_api = None
    _query_api = None
    _delete_api = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance

    def __init__(self):
        if self._client is None:
            self._initialize_client()

    def _initialize_client(self):
        try:
            self._client = InfluxDBClient(
                url=settings.INFLUXDB_URL,
                token=settings.INFLUXDB_TOKEN,
                org=settings.INFLUXDB_ORG,
                timeout=settings.INFLUXDB_TIMEOUT
            )
            self._write_api = self._client.write_api(write_options=SYNCHRONOUS)
            self._query_api = self._client.query_api()
            self._delete_api = self._client.delete_api()
            logger.info("InfluxDB客户端初始化成功")
        except Exception as e:
            logger.error(f"InfluxDB客户端初始化失败: {e}")
            raise

    def write_metrics(self, measurement: str, tags: Dict[str, str],
                      fields: Dict[str, Any], timestamp: Optional[datetime] = None):
        try:
            point = Point(measurement)
            for tag_key, tag_value in tags.items():
                point = point.tag(tag_key, str(tag_value))
            for field_key, field_value in fields.items():
                point = point.field(field_key, field_value)
            if timestamp:
                point = point.time(timestamp)
            else:
                point = point.time(datetime.utcnow())
            self._write_api.write(
                bucket=settings.INFLUXDB_BUCKET,
                org=settings.INFLUXDB_ORG,
                record=point
            )
            logger.debug(f"写入指标成功: {measurement} tags={tags}")
        except Exception as e:
            logger.error(f"写入指标失败: {measurement}, 错误: {e}")
            raise

    def write_batch_metrics(self, points: List[Point]):
        try:
            self._write_api.write(
                bucket=settings.INFLUXDB_BUCKET,
                org=settings.INFLUXDB_ORG,
                record=points
            )
            logger.debug(f"批量写入指标成功, 数量: {len(points)}")
        except Exception as e:
            logger.error(f"批量写入指标失败, 错误: {e}")
            raise

    def query_metrics(self, query: str) -> List[Dict]:
        try:
            result = self._query_api.query(query=query)
            records = []
            for table in result:
                for record in table.records:
                    records.append({
                        "time": record.get_time(),
                        "value": record.get_value(),
                        **record.values
                    })
            return records
        except Exception as e:
            logger.error(f"查询指标失败: {e}")
            raise

    def query_range(self, measurement: str, tags: Dict[str, str],
                    start_time: datetime, end_time: datetime,
                    field_name: Optional[str] = None,
                    aggregation: Optional[str] = None,
                    interval: Optional[str] = None) -> List[Dict]:
        try:
            tag_filters = []
            for key, value in tags.items():
                tag_filters.append(f'r["{key}"] == "{value}"')
            tag_filter_str = " and ".join(tag_filters) if tag_filters else "true"

            field_filter = f'r["_field"] == "{field_name}"' if field_name else "true"

            if aggregation and interval:
                query = f'''
                from(bucket: "{settings.INFLUXDB_BUCKET}")
                    |> range(start: time(v: "{start_time.isoformat()}Z"), stop: time(v: "{end_time.isoformat()}Z"))
                    |> filter(fn: (r) => r["_measurement"] == "{measurement}" and {tag_filter_str} and {field_filter})
                    |> aggregateWindow(every: {interval}, fn: {aggregation}, createEmpty: false)
                    |> yield(name: "{aggregation}")
                '''
            else:
                query = f'''
                from(bucket: "{settings.INFLUXDB_BUCKET}")
                    |> range(start: time(v: "{start_time.isoformat()}Z"), stop: time(v: "{end_time.isoformat()}Z"))
                    |> filter(fn: (r) => r["_measurement"] == "{measurement}" and {tag_filter_str} and {field_filter})
                    |> yield(name: "results")
                '''

            return self.query_metrics(query)
        except Exception as e:
            logger.error(f"范围查询指标失败: {e}")
            raise

    def get_latest_metric(self, measurement: str, tags: Dict[str, str],
                          field_name: str) -> Optional[Dict]:
        try:
            tag_filters = []
            for key, value in tags.items():
                tag_filters.append(f'r["{key}"] == "{value}"')
            tag_filter_str = " and ".join(tag_filters) if tag_filters else "true"

            query = f'''
            from(bucket: "{settings.INFLUXDB_BUCKET}")
                |> range(start: -1h)
                |> filter(fn: (r) => r["_measurement"] == "{measurement}" and {tag_filter_str} and r["_field"] == "{field_name}")
                |> sort(columns: ["_time"], desc: true)
                |> limit(n: 1)
                |> yield(name: "latest")
            '''
            results = self.query_metrics(query)
            return results[0] if results else None
        except Exception as e:
            logger.error(f"获取最新指标失败: {e}")
            return None

    def write_alert(self, alert_data: Dict):
        try:
            point = Point("alerts")
            for key, value in alert_data.items():
                if key not in ["id", "created_at"] and value is not None:
                    if isinstance(value, str):
                        point = point.tag(key, value)
                    else:
                        point = point.field(key, value)
            if "id" in alert_data:
                point = point.tag("alert_id", str(alert_data["id"]))
            point = point.time(datetime.utcnow())
            self._write_api.write(
                bucket=settings.INFLUXDB_BUCKET,
                org=settings.INFLUXDB_ORG,
                record=point
            )
            logger.info(f"写入告警记录成功: {alert_data.get('message', '')}")
        except Exception as e:
            logger.error(f"写入告警记录失败: {e}")
            raise

    def query_alerts(self, start_time: datetime, end_time: datetime,
                     filters: Optional[Dict] = None) -> List[Dict]:
        try:
            filter_conditions = ['r["_measurement"] == "alerts"']
            if filters:
                for key, value in filters.items():
                    if value:
                        filter_conditions.append(f'r["{key}"] == "{value}"')
            filter_str = " and ".join(filter_conditions)

            query = f'''
            from(bucket: "{settings.INFLUXDB_BUCKET}")
                |> range(start: time(v: "{start_time.isoformat()}Z"), stop: time(v: "{end_time.isoformat()}Z"))
                |> filter(fn: (r) => {filter_str})
                |> sort(columns: ["_time"], desc: true)
                |> yield(name: "alerts")
            '''
            return self.query_metrics(query)
        except Exception as e:
            logger.error(f"查询告警记录失败: {e}")
            raise

    def delete_old_metrics(self, retention_days: Optional[int] = None):
        try:
            days = retention_days or settings.METRICS_RETENTION_DAYS
            cutoff_time = datetime.utcnow() - timedelta(days=days)
            self._delete_api.delete(
                start=datetime(2000, 1, 1),
                stop=cutoff_time,
                predicate='_measurement="cpu_usage" or _measurement="memory_usage" or _measurement="disk_usage" or _measurement="network_usage"',
                bucket=settings.INFLUXDB_BUCKET,
                org=settings.INFLUXDB_ORG
            )
            logger.info(f"清理{days}天前的历史指标数据完成")
        except Exception as e:
            logger.error(f"清理历史指标数据失败: {e}")

    def close(self):
        if self._client:
            self._client.close()
            logger.info("InfluxDB客户端连接已关闭")


influxdb_manager = InfluxDBManager()


def get_influxdb_manager() -> InfluxDBManager:
    return influxdb_manager
