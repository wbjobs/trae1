import logging
from typing import List, Optional, Dict
from datetime import datetime, timedelta
from config import settings
from database import InfluxDBManager
from models.schemas import QueryParams, TrendData, TrendPoint, ErrorSummary, ErrorDetail

logger = logging.getLogger(__name__)


class QueryService:

    @staticmethod
    def _escape_string(value: str) -> str:
        if not value:
            return ""
        return value.replace('"', '\\"').replace('\n', '\\n')

    @staticmethod
    def _build_time_range(start_time: datetime, end_time: datetime) -> tuple:
        start_str = start_time.strftime('%Y-%m-%dT%H:%M:%SZ')
        end_str = end_time.strftime('%Y-%m-%dT%H:%M:%SZ')
        return start_str, end_str

    @staticmethod
    def get_performance_trend(params: QueryParams, metric: str) -> TrendData:
        start_str, end_str = QueryService._build_time_range(params.start_time, params.end_time)

        flux_query = f'''
from(bucket: "{settings.INFLUXDB_BUCKET}")
    |> range(start: time(v: "{start_str}"), stop: time(v: "{end_str}"))
    |> filter(fn: (r) => r["_measurement"] == "performance_metrics")
    |> filter(fn: (r) => r["_field"] == "{metric}")
    |> filter(fn: (r) => r["app_id"] == "{params.app_id}")
'''

        if params.page_url:
            escaped_url = QueryService._escape_string(params.page_url)
            flux_query += f'    |> filter(fn: (r) => r["page_url"] == "{escaped_url}")\n'

        flux_query += '    |> keep(columns: ["_time", "_value"])\n'
        flux_query += '    |> sort(columns: ["_time"])'

        tables = InfluxDBManager.query(flux_query)

        if tables is None:
            return TrendData(
                metric=metric,
                points=[],
                avg=0,
                min=0,
                max=0,
                p50=0,
                p95=0
            )

        points = []
        values = []

        try:
            for table in tables:
                for record in table.records:
                    try:
                        time_val = record.get_time()
                        value = float(record.get_value())
                        points.append(TrendPoint(
                            time=time_val,
                            value=value
                        ))
                        values.append(value)
                    except (ValueError, TypeError) as e:
                        logger.warning(f"Invalid record value: {e}")
                        continue
        except Exception as e:
            logger.error(f"Error parsing query results: {e}")

        if not values:
            return TrendData(
                metric=metric,
                points=[],
                avg=0,
                min=0,
                max=0,
                p50=0,
                p95=0
            )

        sorted_values = sorted(values)
        avg = sum(values) / len(values)
        min_val = min(values)
        max_val = max(values)

        p50_idx = max(0, min(int(len(sorted_values) * 0.5), len(sorted_values) - 1))
        p95_idx = max(0, min(int(len(sorted_values) * 0.95), len(sorted_values) - 1))

        p50 = sorted_values[p50_idx]
        p95 = sorted_values[p95_idx]

        return TrendData(
            metric=metric,
            points=points,
            avg=round(avg, 2),
            min=round(min_val, 2),
            max=round(max_val, 2),
            p50=round(p50, 2),
            p95=round(p95, 2)
        )

    @staticmethod
    def get_all_performance_trends(params: QueryParams) -> Dict[str, TrendData]:
        metrics = ["fp", "fcp", "lcp", "ttfb", "dom_ready", "load_time"]
        result = {}

        for metric in metrics:
            try:
                trend = QueryService.get_performance_trend(params, metric)
                if trend.points:
                    result[metric] = trend
            except Exception as e:
                logger.error(f"Error getting trend for {metric}: {e}")

        return result

    @staticmethod
    def get_error_summary(params: QueryParams) -> List[ErrorSummary]:
        start_str, end_str = QueryService._build_time_range(params.start_time, params.end_time)

        flux_query = f'''
from(bucket: "{settings.INFLUXDB_BUCKET}")
    |> range(start: time(v: "{start_str}"), stop: time(v: "{end_str}"))
    |> filter(fn: (r) => r["_measurement"] == "error_events")
    |> filter(fn: (r) => r["app_id"] == "{params.app_id}")
    |> keep(columns: ["_time", "error_type", "error_message"])
'''

        if params.page_url:
            escaped_url = QueryService._escape_string(params.page_url)
            flux_query += f'    |> filter(fn: (r) => r["page_url"] == "{escaped_url}")\n'

        tables = InfluxDBManager.query(flux_query)

        if tables is None:
            return []

        error_groups: Dict[str, dict] = {}

        try:
            for table in tables:
                for record in table.records:
                    try:
                        error_type = record.values.get("error_type", "unknown")
                        if error_type not in error_groups:
                            error_groups[error_type] = {
                                "count": 0,
                                "last_occurrence": None,
                                "messages": set()
                            }

                        error_groups[error_type]["count"] += 1
                        record_time = record.get_time()

                        if (error_groups[error_type]["last_occurrence"] is None or
                                record_time > error_groups[error_type]["last_occurrence"]):
                            error_groups[error_type]["last_occurrence"] = record_time

                        message = record.values.get("error_message", "")
                        if message:
                            error_groups[error_type]["messages"].add(str(message)[:500])
                    except Exception as e:
                        logger.warning(f"Error processing error record: {e}")
        except Exception as e:
            logger.error(f"Error parsing error summary results: {e}")

        result = []
        for error_type, data in error_groups.items():
            result.append(ErrorSummary(
                error_type=error_type,
                count=data["count"],
                last_occurrence=data["last_occurrence"] or datetime.now(),
                messages=list(data["messages"])[:10]
            ))

        result.sort(key=lambda x: x.count, reverse=True)
        return result

    @staticmethod
    def get_error_details(params: QueryParams, error_type: Optional[str] = None,
                          limit: int = 100, offset: int = 0) -> List[ErrorDetail]:
        start_str, end_str = QueryService._build_time_range(params.start_time, params.end_time)

        flux_query = f'''
from(bucket: "{settings.INFLUXDB_BUCKET}")
    |> range(start: time(v: "{start_str}"), stop: time(v: "{end_str}"))
    |> filter(fn: (r) => r["_measurement"] == "error_events")
    |> filter(fn: (r) => r["app_id"] == "{params.app_id}")
'''

        if error_type:
            escaped_type = QueryService._escape_string(error_type)
            flux_query += f'    |> filter(fn: (r) => r["error_type"] == "{escaped_type}")\n'

        if params.page_url:
            escaped_url = QueryService._escape_string(params.page_url)
            flux_query += f'    |> filter(fn: (r) => r["page_url"] == "{escaped_url}")\n'

        flux_query += f'    |> keep(columns: ["_time", "_field", "_value", "error_type", "page_url", "user_id", "error_message"])\n'
        flux_query += f'    |> sort(columns: ["_time"], desc: true)\n'
        flux_query += f'    |> limit(n: {limit}, offset: {offset})'

        tables = InfluxDBManager.query(flux_query)

        if tables is None:
            return []

        error_dict: Dict[str, dict] = {}

        try:
            for table in tables:
                for record in table.records:
                    try:
                        time_key = record.get_time().isoformat()

                        if time_key not in error_dict:
                            error_dict[time_key] = {
                                "timestamp": record.get_time(),
                                "error_type": record.values.get("error_type", ""),
                                "message": record.values.get("error_message", ""),
                                "stack": None,
                                "filename": None,
                                "lineno": None,
                                "colno": None,
                                "page_url": record.values.get("page_url", ""),
                                "user_id": record.values.get("user_id", None)
                            }

                        field = record.get_field()
                        value = record.get_value()

                        if field == "stack" and value:
                            error_dict[time_key]["stack"] = str(value)
                        elif field == "filename" and value:
                            error_dict[time_key]["filename"] = str(value)
                        elif field == "lineno" and value:
                            error_dict[time_key]["lineno"] = int(value)
                        elif field == "colno" and value:
                            error_dict[time_key]["colno"] = int(value)
                    except Exception as e:
                        logger.warning(f"Error processing error detail record: {e}")
        except Exception as e:
            logger.error(f"Error parsing error details results: {e}")

        result = []
        for time_key, data in sorted(error_dict.items(), reverse=True):
            result.append(ErrorDetail(
                id=time_key,
                timestamp=data["timestamp"],
                error_type=data["error_type"],
                message=data["message"],
                stack=data["stack"],
                filename=data["filename"],
                lineno=data["lineno"],
                colno=data["colno"],
                page_url=data["page_url"],
                user_id=data["user_id"]
            ))

        return result

    @staticmethod
    def get_renderer_summary(params: QueryParams) -> dict:
        start_str, end_str = QueryService._build_time_range(params.start_time, params.end_time)

        flux_query = f'''
from(bucket: "{settings.INFLUXDB_BUCKET}")
    |> range(start: time(v: "{start_str}"), stop: time(v: "{end_str}"))
    |> filter(fn: (r) => r["_measurement"] == "renderer_metrics")
    |> filter(fn: (r) => r["app_id"] == "{params.app_id}")
    |> keep(columns: ["_time", "_field", "_value"])
'''

        if params.page_url:
            escaped_url = QueryService._escape_string(params.page_url)
            flux_query += f'    |> filter(fn: (r) => r["page_url"] == "{escaped_url}")\n'

        tables = InfluxDBManager.query(flux_query)

        if tables is None:
            return {
                "avg_fps": 0,
                "min_fps": 0,
                "max_fps": 0,
                "total_jank": 0,
                "total_long_tasks": 0
            }

        fps_values = []
        jank_total = 0
        long_task_total = 0

        try:
            for table in tables:
                for record in table.records:
                    try:
                        field = record.get_field()
                        value = float(record.get_value())

                        if field == "fps":
                            fps_values.append(value)
                        elif field == "jank_count":
                            jank_total += value
                        elif field == "long_task_count":
                            long_task_total += value
                    except (ValueError, TypeError) as e:
                        logger.warning(f"Invalid renderer value: {e}")
        except Exception as e:
            logger.error(f"Error parsing renderer results: {e}")

        avg_fps = sum(fps_values) / len(fps_values) if fps_values else 0

        return {
            "avg_fps": round(avg_fps, 2),
            "min_fps": round(min(fps_values), 2) if fps_values else 0,
            "max_fps": round(max(fps_values), 2) if fps_values else 0,
            "total_jank": int(jank_total),
            "total_long_tasks": int(long_task_total)
        }

    @staticmethod
    def get_app_stats(params: QueryParams) -> dict:
        start_str, end_str = QueryService._build_time_range(params.start_time, params.end_time)

        page_views_query = f'''
from(bucket: "{settings.INFLUXDB_BUCKET}")
    |> range(start: time(v: "{start_str}"), stop: time(v: "{end_str}"))
    |> filter(fn: (r) => r["_measurement"] == "performance_metrics")
    |> filter(fn: (r) => r["app_id"] == "{params.app_id}")
    |> keep(columns: ["session_id"])
'''

        pv_count = 0
        try:
            tables = InfluxDBManager.query(page_views_query)
            if tables:
                unique_sessions = set()
                for table in tables:
                    for record in table.records:
                        session_id = record.values.get("session_id", "")
                        if session_id:
                            unique_sessions.add(session_id)
                pv_count = len(unique_sessions)
        except Exception as e:
            logger.error(f"Error counting page views: {e}")

        error_count = 0
        try:
            error_query = f'''
from(bucket: "{settings.INFLUXDB_BUCKET}")
    |> range(start: time(v: "{start_str}"), stop: time(v: "{end_str}"))
    |> filter(fn: (r) => r["_measurement"] == "error_events")
    |> filter(fn: (r) => r["app_id"] == "{params.app_id}")
    |> count()
'''
            error_tables = InfluxDBManager.query(error_query)
            if error_tables:
                for table in error_tables:
                    for record in table.records:
                        error_count += int(record.get_value() or 0)
        except Exception as e:
            logger.error(f"Error counting errors: {e}")

        return {
            "page_views": pv_count,
            "error_count": error_count,
            "time_range": {
                "start": params.start_time.isoformat(),
                "end": params.end_time.isoformat()
            }
        }
