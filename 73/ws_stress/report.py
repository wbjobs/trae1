"""JSON 报告输出模块"""

import json
import os
import time
from typing import List, Union

from .config import TestReport


class ReportGenerator:
    @staticmethod
    def save_json(report: Union[TestReport, dict], output_dir: str, prefix: str = "report") -> str:
        os.makedirs(output_dir, exist_ok=True)
        filename = f"{prefix}_{int(time.time())}.json"
        filepath = os.path.join(output_dir, filename)

        if isinstance(report, TestReport):
            report_dict = {
                "node_id": report.node_id,
                "start_time": report.start_time,
                "end_time": report.end_time,
                "duration_seconds": round(report.end_time - report.start_time, 2),
                "config": report.config,
                "connection": {
                    "success_rate": report.connection_success_rate,
                    "total": report.total_connections,
                    "successful": report.successful_connections,
                    "failed": report.failed_connections,
                },
                "message": {
                    "arrival_rate": report.message_arrival_rate,
                    "total_sent": report.total_sent,
                    "total_received": report.total_received,
                },
                "latency_ms": {
                    "avg": report.latency_avg,
                    "min": report.latency_min,
                    "max": report.latency_max,
                    "p50": report.latency_p50,
                    "p95": report.latency_p95,
                    "p99": report.latency_p99,
                },
                "reconnect": {
                    "attempts": report.reconnect_attempts,
                    "successes": report.reconnect_successes,
                    "success_rate": report.reconnect_success_rate,
                    "per_client_avg": report.reconnect_per_client_avg,
                },
                "throughput_curve": report.throughput_curve,
                "latency_distribution": report.latency_distribution,
            }
        else:
            report_dict = report

        with open(filepath, "w", encoding="utf-8") as f:
            json.dump(report_dict, f, indent=2, ensure_ascii=False, default=str)
        return filepath

    @staticmethod
    def aggregate_reports(reports: List[dict], config: dict) -> dict:
        if not reports:
            return {}
        total_conn = sum(r.get("connection", {}).get("total", 0) for r in reports)
        total_succ = sum(r.get("connection", {}).get("successful", 0) for r in reports)
        total_fail = sum(r.get("connection", {}).get("failed", 0) for r in reports)
        total_sent = sum(r.get("message", {}).get("total_sent", 0) for r in reports)
        total_recv = sum(r.get("message", {}).get("total_received", 0) for r in reports)
        total_reconnect_attempts = sum(r.get("reconnect", {}).get("attempts", 0) for r in reports)
        total_reconnect_successes = sum(r.get("reconnect", {}).get("successes", 0) for r in reports)

        all_latencies = []
        for r in reports:
            lat = r.get("latency_ms", {})
            if lat.get("min") is not None:
                all_latencies.append(lat.get("min"))
                all_latencies.append(lat.get("max"))
                all_latencies.append(lat.get("avg"))
                all_latencies.append(lat.get("p50"))
                all_latencies.append(lat.get("p95"))
                all_latencies.append(lat.get("p99"))

        reconnect_rate = (
            round(total_reconnect_successes / total_reconnect_attempts * 100, 2)
            if total_reconnect_attempts > 0 else 0.0
        )

        agg = {
            "aggregate": True,
            "timestamp": time.time(),
            "node_count": len(reports),
            "config": config,
            "connection": {
                "success_rate": round(total_succ / total_conn * 100, 2) if total_conn > 0 else 0,
                "total": total_conn,
                "successful": total_succ,
                "failed": total_fail,
            },
            "message": {
                "arrival_rate": round(total_recv / total_sent * 100, 2) if total_sent > 0 else 0,
                "total_sent": total_sent,
                "total_received": total_recv,
            },
            "reconnect": {
                "attempts": total_reconnect_attempts,
                "successes": total_reconnect_successes,
                "success_rate": reconnect_rate,
            },
            "node_reports": reports,
        }
        return agg

    @staticmethod
    def save_aggregate(agg_report: dict, output_dir: str) -> str:
        os.makedirs(output_dir, exist_ok=True)
        filepath = os.path.join(output_dir, f"aggregate_{int(time.time())}.json")
        with open(filepath, "w", encoding="utf-8") as f:
            json.dump(agg_report, f, indent=2, ensure_ascii=False, default=str)
        return filepath
