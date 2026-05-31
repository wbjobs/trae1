import logging
from typing import List, Dict, Any, Optional
from datetime import datetime
from influxdb_client import Point
from database import InfluxDBManager
from models.schemas import (
    PerformanceReport,
    ErrorReport,
    RendererReport,
    PerformanceMetrics,
    ErrorInfo,
    RendererInfo
)
from services.data_cleaning_service import DataCleaningService

logger = logging.getLogger(__name__)


class DataReportService:

    @staticmethod
    def _validate_report(report_data: dict, required_fields: List[str]) -> bool:
        for field in required_fields:
            if field not in report_data or report_data[field] is None:
                return False
        return True

    @staticmethod
    def report_performance(report: PerformanceReport) -> dict:
        try:
            report_dict = report.model_dump() if hasattr(report, 'model_dump') else report.dict()

            cleaned_data = DataCleaningService.clean_performance_data(report_dict)
            if not cleaned_data:
                return {"status": "error", "message": "Invalid performance data after cleaning"}

            cleaned_report = PerformanceReport(**cleaned_data)

            anomalies = DataCleaningService.detect_anomalies(cleaned_data.get('metrics', {}))
            if anomalies:
                logger.warning(f"Anomalies detected in performance data: {anomalies}")

            tags = {
                "app_id": cleaned_report.app_id or "unknown",
                "page_url": (cleaned_report.page_url or "unknown")[:512],
                "user_id": cleaned_report.user_id or "anonymous",
                "session_id": cleaned_report.session_id or "unknown"
            }

            fields = {}
            metrics = cleaned_report.metrics

            if metrics is None:
                return {"status": "error", "message": "No metrics data provided"}

            metric_mapping = {
                'fp': metrics.fp,
                'fcp': metrics.fcp,
                'lcp': metrics.lcp,
                'ttfb': metrics.ttfb,
                'dom_ready': metrics.dom_ready,
                'load_time': metrics.load_time
            }

            for key, value in metric_mapping.items():
                if value is not None:
                    try:
                        fields[key] = float(value)
                    except (ValueError, TypeError):
                        logger.warning(f"Invalid metric value for {key}: {value}")

            if not fields:
                return {"status": "error", "message": "No valid metrics to store"}

            success = InfluxDBManager.write_point(
                measurement="performance_metrics",
                tags=tags,
                fields=fields,
                timestamp=cleaned_report.timestamp
            )

            if success:
                return {
                    "status": "success",
                    "message": "Performance data stored",
                    "metrics_count": len(fields),
                    "anomalies": anomalies
                }
            else:
                return {"status": "error", "message": "Failed to store performance data"}

        except Exception as e:
            logger.error(f"Error processing performance report: {e}")
            return {"status": "error", "message": str(e)}

    @staticmethod
    def report_error(report: ErrorReport) -> dict:
        try:
            report_dict = report.model_dump() if hasattr(report, 'model_dump') else report.dict()

            cleaned_data = DataCleaningService.clean_error_data(report_dict)
            if not cleaned_data:
                return {"status": "error", "message": "Invalid error data after cleaning"}

            cleaned_report = ErrorReport(**cleaned_data)

            tags = {
                "app_id": cleaned_report.app_id or "unknown",
                "page_url": (cleaned_report.page_url or "unknown")[:512],
                "user_id": cleaned_report.user_id or "anonymous",
                "session_id": cleaned_report.session_id or "unknown",
                "error_type": cleaned_report.error.error_type or "unknown",
                "error_message": (cleaned_report.error.message or "unknown")[:255]
            }

            fields = {
                "message": cleaned_report.error.message or "",
                "error_type": cleaned_report.error.error_type or "unknown"
            }

            if cleaned_report.error.stack:
                fields["stack"] = str(cleaned_report.error.stack)[:5000]
            if cleaned_report.error.filename:
                fields["filename"] = str(cleaned_report.error.filename)[:512]
            if cleaned_report.error.lineno is not None:
                fields["lineno"] = int(cleaned_report.error.lineno)
            if cleaned_report.error.colno is not None:
                fields["colno"] = int(cleaned_report.error.colno)

            success = InfluxDBManager.write_point(
                measurement="error_events",
                tags=tags,
                fields=fields,
                timestamp=cleaned_report.timestamp
            )

            if success:
                return {"status": "success", "message": "Error data stored"}
            else:
                return {"status": "error", "message": "Failed to store error data"}

        except Exception as e:
            logger.error(f"Error processing error report: {e}")
            return {"status": "error", "message": str(e)}

    @staticmethod
    def report_renderer(report: RendererReport) -> dict:
        try:
            report_dict = report.model_dump() if hasattr(report, 'model_dump') else report.dict()

            cleaned_data = DataCleaningService.clean_renderer_data(report_dict)
            if not cleaned_data:
                return {"status": "error", "message": "Invalid renderer data after cleaning"}

            cleaned_report = RendererReport(**cleaned_data)

            tags = {
                "app_id": cleaned_report.app_id or "unknown",
                "page_url": (cleaned_report.page_url or "unknown")[:512],
                "user_id": cleaned_report.user_id or "anonymous",
                "session_id": cleaned_report.session_id or "unknown"
            }

            fields = {
                "fps": float(cleaned_report.renderer.fps),
                "long_task_count": int(cleaned_report.renderer.long_task_count),
                "jank_count": int(cleaned_report.renderer.jank_count)
            }

            if cleaned_report.renderer.memory_used is not None:
                fields["memory_used"] = float(cleaned_report.renderer.memory_used)

            success = InfluxDBManager.write_point(
                measurement="renderer_metrics",
                tags=tags,
                fields=fields,
                timestamp=cleaned_report.timestamp
            )

            if success:
                return {"status": "success", "message": "Renderer data stored"}
            else:
                return {"status": "error", "message": "Failed to store renderer data"}

        except Exception as e:
            logger.error(f"Error processing renderer report: {e}")
            return {"status": "error", "message": str(e)}

    @staticmethod
    def report_batch(reports: List[dict], report_type: str) -> dict:
        if not reports:
            return {"status": "success", "message": "No data to process"}

        points = []
        success_count = 0
        error_count = 0

        for item in reports:
            try:
                if report_type == "performance":
                    point = DataReportService._build_performance_point(item)
                    if point:
                        points.append(point)
                        success_count += 1

                elif report_type == "errors":
                    point = DataReportService._build_error_point(item)
                    if point:
                        points.append(point)
                        success_count += 1

                elif report_type == "renderer":
                    point = DataReportService._build_renderer_point(item)
                    if point:
                        points.append(point)
                        success_count += 1

                else:
                    error_count += 1

            except Exception as e:
                logger.error(f"Error processing batch item: {e}")
                error_count += 1

        if points:
            batch_success = InfluxDBManager.write_points(points)
            if not batch_success:
                return {
                    "status": "error",
                    "message": "Failed to store batch data",
                    "success_count": success_count,
                    "error_count": error_count
                }

        return {
            "status": "success",
            "message": f"Batch processed: {success_count} success, {error_count} errors",
            "success_count": success_count,
            "error_count": error_count,
            "total_points": len(points)
        }

    @staticmethod
    def _build_performance_point(item: dict) -> Optional[Point]:
        try:
            report = PerformanceReport(**item)
            metrics = report.metrics

            if not metrics:
                return None

            metric_mapping = {
                'fp': metrics.fp,
                'fcp': metrics.fcp,
                'lcp': metrics.lcp,
                'ttfb': metrics.ttfb,
                'dom_ready': metrics.dom_ready,
                'load_time': metrics.load_time
            }

            has_valid_metric = any(v is not None for v in metric_mapping.values())
            if not has_valid_metric:
                return None

            point = Point("performance_metrics")
            point = point.tag("app_id", report.app_id or "unknown")
            point = point.tag("page_url", (report.page_url or "unknown")[:512])
            point = point.tag("user_id", report.user_id or "anonymous")
            point = point.tag("session_id", report.session_id or "unknown")

            for key, value in metric_mapping.items():
                if value is not None:
                    point = point.field(key, float(value))

            point = point.time(report.timestamp)
            return point

        except Exception as e:
            logger.error(f"Error building performance point: {e}")
            return None

    @staticmethod
    def _build_error_point(item: dict) -> Optional[Point]:
        try:
            report = ErrorReport(**item)

            point = Point("error_events")
            point = point.tag("app_id", report.app_id or "unknown")
            point = point.tag("page_url", (report.page_url or "unknown")[:512])
            point = point.tag("user_id", report.user_id or "anonymous")
            point = point.tag("session_id", report.session_id or "unknown")
            point = point.tag("error_type", report.error.error_type or "unknown")
            point = point.tag("error_message", (report.error.message or "unknown")[:255])

            point = point.field("message", report.error.message or "")
            point = point.field("error_type", report.error.error_type or "unknown")

            if report.error.stack:
                point = point.field("stack", str(report.error.stack)[:5000])
            if report.error.filename:
                point = point.field("filename", str(report.error.filename)[:512])
            if report.error.lineno is not None:
                point = point.field("lineno", int(report.error.lineno))
            if report.error.colno is not None:
                point = point.field("colno", int(report.error.colno))

            point = point.time(report.timestamp)
            return point

        except Exception as e:
            logger.error(f"Error building error point: {e}")
            return None

    @staticmethod
    def _build_renderer_point(item: dict) -> Optional[Point]:
        try:
            report = RendererReport(**item)

            point = Point("renderer_metrics")
            point = point.tag("app_id", report.app_id or "unknown")
            point = point.tag("page_url", (report.page_url or "unknown")[:512])
            point = point.tag("user_id", report.user_id or "anonymous")
            point = point.tag("session_id", report.session_id or "unknown")

            point = point.field("fps", float(report.renderer.fps))
            point = point.field("long_task_count", int(report.renderer.long_task_count))
            point = point.field("jank_count", int(report.renderer.jank_count))

            if report.renderer.memory_used is not None:
                point = point.field("memory_used", float(report.renderer.memory_used))

            point = point.time(report.timestamp)
            return point

        except Exception as e:
            logger.error(f"Error building renderer point: {e}")
            return None
