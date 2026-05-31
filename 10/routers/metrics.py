import logging
import asyncio
from fastapi import APIRouter, HTTPException, Query
from datetime import datetime, timedelta
from typing import Optional, List

from models.schemas import (
    MetricsQuery, MetricsResponse, TrendAnalysisResult,
    BatchCollectRequest, RankingResult, DenoiseConfigUpdate
)
from db.influxdb_client import get_influxdb_manager
from services.metrics_service import get_metrics_service
from services.batch_collector import get_batch_collector
from services.ranking import get_ranking_service
from services.filter import DataFilter
from config import settings

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/metrics", tags=["指标采集"])


@router.post("/collect", summary="采集服务器指标", response_model=MetricsResponse)
async def collect_metrics():
    try:
        service = get_metrics_service()
        result = await asyncio.to_thread(service.collect_and_report)

        if result.get("success"):
            return MetricsResponse(
                success=True,
                message="指标采集上报成功",
                data=result
            )
        else:
            return MetricsResponse(
                success=False,
                message=result.get("message", "指标采集上报失败")
            )
    except Exception as e:
        logger.error(f"采集指标失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/collect/cpu", summary="采集CPU指标", response_model=MetricsResponse)
async def collect_cpu_metrics():
    try:
        service = get_metrics_service()
        result = await asyncio.to_thread(service.collect_cpu)
        return MetricsResponse(
            success=True,
            message="CPU指标采集成功",
            data=result
        )
    except Exception as e:
        logger.error(f"采集CPU指标失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/collect/memory", summary="采集内存指标", response_model=MetricsResponse)
async def collect_memory_metrics():
    try:
        service = get_metrics_service()
        result = await asyncio.to_thread(service.collect_memory)
        return MetricsResponse(
            success=True,
            message="内存指标采集成功",
            data=result
        )
    except Exception as e:
        logger.error(f"采集内存指标失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/collect/disk", summary="采集磁盘指标", response_model=MetricsResponse)
async def collect_disk_metrics():
    try:
        service = get_metrics_service()
        result = await asyncio.to_thread(service.collect_disk)
        return MetricsResponse(
            success=True,
            message="磁盘指标采集成功",
            data=result
        )
    except Exception as e:
        logger.error(f"采集磁盘指标失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/collect/network", summary="采集网络指标", response_model=MetricsResponse)
async def collect_network_metrics():
    try:
        service = get_metrics_service()
        result = await asyncio.to_thread(service.collect_network)
        return MetricsResponse(
            success=True,
            message="网络指标采集成功",
            data=result
        )
    except Exception as e:
        logger.error(f"采集网络指标失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/collect/batch", summary="批量采集多服务器指标")
async def batch_collect(request: BatchCollectRequest):
    try:
        batch = get_batch_collector()
        targets = [
            {
                'hostname': t.hostname,
                'url': t.url or f'http://{t.hostname}:8000'
            }
            for t in request.targets
        ]
        result = await batch.collect_all(targets)
        return {
            "success": True,
            "message": result.get('message', '批量采集完成'),
            "data": result
        }
    except Exception as e:
        logger.error(f"批量采集失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/query", summary="查询指标数据")
async def query_metrics(
    hostname: str = Query(..., description="主机名"),
    metric_type: str = Query(..., description="指标类型: cpu/memory/disk/network"),
    start_time: Optional[datetime] = Query(None, description="开始时间"),
    end_time: Optional[datetime] = Query(None, description="结束时间"),
    metric_name: Optional[str] = Query(None, description="指标名称"),
    aggregation: Optional[str] = Query(None, description="聚合方式: mean/max/min/sum/count"),
    interval: Optional[str] = Query(None, description="聚合间隔: 1m/5m/1h")
):
    try:
        db = get_influxdb_manager()

        if not start_time:
            start_time = datetime.utcnow() - timedelta(hours=1)
        if not end_time:
            end_time = datetime.utcnow()

        measurement_map = {
            "cpu": "cpu_usage",
            "memory": "memory_usage",
            "disk": "disk_usage",
            "network": "network_usage"
        }

        measurement = measurement_map.get(metric_type)
        if not measurement:
            raise HTTPException(status_code=400, detail=f"不支持的指标类型: {metric_type}")

        tags = {"hostname": hostname}
        results = db.query_range(
            measurement=measurement,
            tags=tags,
            start_time=start_time,
            end_time=end_time,
            field_name=metric_name,
            aggregation=aggregation,
            interval=interval
        )

        return {
            "success": True,
            "message": "查询成功",
            "data": {
                "hostname": hostname,
                "metric_type": metric_type,
                "metric_name": metric_name,
                "start_time": start_time.isoformat(),
                "end_time": end_time.isoformat(),
                "results": results,
                "count": len(results)
            }
        }
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"查询指标失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/latest/{hostname}", summary="获取最新指标数据")
async def get_latest_metrics(hostname: str):
    try:
        db = get_influxdb_manager()
        measurements = ["cpu_usage", "memory_usage", "disk_usage", "network_usage"]
        latest_data = {}

        for measurement in measurements:
            field_names = {
                "cpu_usage": "usage_percent",
                "memory_usage": "usage_percent",
                "disk_usage": "usage_percent",
                "network_usage": "bytes_recv"
            }
            result = db.get_latest_metric(
                measurement=measurement,
                tags={"hostname": hostname},
                field_name=field_names[measurement]
            )
            if result:
                latest_data[measurement] = result

        return {
            "success": True,
            "message": "获取最新指标成功",
            "data": {
                "hostname": hostname,
                "metrics": latest_data
            }
        }
    except Exception as e:
        logger.error(f"获取最新指标失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/trend", summary="趋势分析")
async def trend_analysis(
    hostname: str = Query(..., description="主机名"),
    metric_type: str = Query(..., description="指标类型: cpu/memory/disk/network"),
    metric_name: str = Query(..., description="指标名称"),
    hours: int = Query(24, description="分析时长(小时)")
):
    try:
        db = get_influxdb_manager()
        end_time = datetime.utcnow()
        start_time = end_time - timedelta(hours=hours)

        measurement_map = {
            "cpu": "cpu_usage",
            "memory": "memory_usage",
            "disk": "disk_usage",
            "network": "network_usage"
        }

        measurement = measurement_map.get(metric_type)
        if not measurement:
            raise HTTPException(status_code=400, detail=f"不支持的指标类型: {metric_type}")

        results = db.query_range(
            measurement=measurement,
            tags={"hostname": hostname},
            start_time=start_time,
            end_time=end_time,
            field_name=metric_name,
            aggregation="mean",
            interval="1h"
        )

        if not results:
            return {
                "success": True,
                "message": "无数据可分析",
                "data": None
            }

        values = [r.get("_value", 0) for r in results]

        current_value = values[-1] if values else 0
        average_value = sum(values) / len(values) if values else 0
        max_value = max(values) if values else 0
        min_value = min(values) if values else 0

        trend_direction = "stable"
        trend_rate = 0.0
        if len(values) >= 2:
            first_half = values[:len(values)//2]
            second_half = values[len(values)//2:]
            avg_first = sum(first_half) / len(first_half) if first_half else 0
            avg_second = sum(second_half) / len(second_half) if second_half else 0

            if avg_first > 0:
                trend_rate = ((avg_second - avg_first) / avg_first) * 100

            if trend_rate > 5:
                trend_direction = "increasing"
            elif trend_rate < -5:
                trend_direction = "decreasing"

        prediction_24h = None
        if len(values) >= 4:
            prediction_24h = current_value + (current_value - values[0]) * (24 / hours)

        result = TrendAnalysisResult(
            hostname=hostname,
            metric_type=metric_type,
            metric_name=metric_name,
            current_value=current_value,
            average_value=average_value,
            max_value=max_value,
            min_value=min_value,
            trend_direction=trend_direction,
            trend_rate=trend_rate,
            prediction_24h=prediction_24h,
            data_points=len(values)
        )

        return {
            "success": True,
            "message": "趋势分析完成",
            "data": result.model_dump()
        }
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"趋势分析失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/ranking", summary="资源占用排行分析")
async def resource_ranking(
    minutes: int = Query(5, description="统计时间窗口(分钟)"),
    top_n: int = Query(20, description="返回前N名")
):
    try:
        ranking = get_ranking_service()
        result = ranking.get_all_rankings(minutes=minutes, top_n=top_n)
        return {
            "success": True,
            "message": "排行分析完成",
            "data": result
        }
    except Exception as e:
        logger.error(f"排行分析失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/ranking/cpu", summary="CPU占用排行")
async def cpu_ranking(
    minutes: int = Query(5, description="统计时间窗口(分钟)"),
    top_n: int = Query(20, description="返回前N名")
):
    try:
        ranking = get_ranking_service()
        result = ranking.get_cpu_ranking(minutes=minutes, top_n=top_n)
        return {
            "success": True,
            "message": "CPU排行获取成功",
            "data": result
        }
    except Exception as e:
        logger.error(f"CPU排行获取失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/ranking/memory", summary="内存占用排行")
async def memory_ranking(
    minutes: int = Query(5, description="统计时间窗口(分钟)"),
    top_n: int = Query(20, description="返回前N名")
):
    try:
        ranking = get_ranking_service()
        result = ranking.get_memory_ranking(minutes=minutes, top_n=top_n)
        return {
            "success": True,
            "message": "内存排行获取成功",
            "data": result
        }
    except Exception as e:
        logger.error(f"内存排行获取失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/ranking/disk", summary="磁盘占用排行")
async def disk_ranking(
    minutes: int = Query(5, description="统计时间窗口(分钟)"),
    top_n: int = Query(20, description="返回前N名")
):
    try:
        ranking = get_ranking_service()
        result = ranking.get_disk_ranking(minutes=minutes, top_n=top_n)
        return {
            "success": True,
            "message": "磁盘排行获取成功",
            "data": result
        }
    except Exception as e:
        logger.error(f"磁盘排行获取失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/ranking/disk-io", summary="磁盘IO排行")
async def disk_io_ranking(
    minutes: int = Query(5, description="统计时间窗口(分钟)"),
    top_n: int = Query(20, description="返回前N名")
):
    try:
        ranking = get_ranking_service()
        result = ranking.get_disk_io_ranking(minutes=minutes, top_n=top_n)
        return {
            "success": True,
            "message": "磁盘IO排行获取成功",
            "data": result
        }
    except Exception as e:
        logger.error(f"磁盘IO排行获取失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/ranking/network", summary="网络流量排行")
async def network_ranking(
    minutes: int = Query(5, description="统计时间窗口(分钟)"),
    top_n: int = Query(20, description="返回前N名")
):
    try:
        ranking = get_ranking_service()
        result = ranking.get_network_ranking(minutes=minutes, top_n=top_n)
        return {
            "success": True,
            "message": "网络排行获取成功",
            "data": result
        }
    except Exception as e:
        logger.error(f"网络排行获取失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/overloaded", summary="获取过载服务器列表")
async def get_overloaded_hosts(
    minutes: int = Query(5, description="统计时间窗口(分钟)"),
    cpu_threshold: float = Query(None, description="CPU过载阈值"),
    memory_threshold: float = Query(None, description="内存过载阈值"),
    disk_threshold: float = Query(None, description="磁盘过载阈值")
):
    try:
        ranking = get_ranking_service()
        result = ranking.get_overloaded_hosts(
            minutes=minutes,
            cpu_threshold=cpu_threshold,
            memory_threshold=memory_threshold,
            disk_threshold=disk_threshold
        )
        return {
            "success": True,
            "message": f"发现 {len(result)} 个过载资源",
            "data": {
                "overloaded_count": len(result),
                "hosts": result
            }
        }
    except Exception as e:
        logger.error(f"获取过载服务器失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/denoise/config", summary="获取降噪配置")
async def get_denoise_config():
    return {
        "success": True,
        "data": {
            "enabled": settings.NOISE_REDUCTION_ENABLED,
            "method": "ema",
            "ema_alpha": settings.NOISE_REDUCTION_EMA_ALPHA,
            "sample_count": settings.NOISE_REDUCTION_SAMPLE_COUNT,
            "outlier_std_threshold": settings.NOISE_REDUCTION_OUTLIER_STD_THRESHOLD,
            "kalman_process_noise": settings.KALMAN_PROCESS_NOISE,
            "kalman_measurement_noise": settings.KALMAN_MEASUREMENT_NOISE,
            "available_methods": ["ema", "kalman", "median", "savgol"]
        }
    }


@router.post("/denoise/config", summary="更新降噪配置")
async def update_denoise_config(config: DenoiseConfigUpdate):
    try:
        if config.ema_alpha is not None:
            settings.NOISE_REDUCTION_EMA_ALPHA = config.ema_alpha
        if config.sample_count is not None:
            settings.NOISE_REDUCTION_SAMPLE_COUNT = config.sample_count
        settings.NOISE_REDUCTION_ENABLED = config.enabled
        DataFilter.reset_all_filters()
        return {
            "success": True,
            "message": "降噪配置已更新",
            "data": {
                "enabled": settings.NOISE_REDUCTION_ENABLED,
                "method": config.method,
                "ema_alpha": settings.NOISE_REDUCTION_EMA_ALPHA,
                "sample_count": settings.NOISE_REDUCTION_SAMPLE_COUNT
            }
        }
    except Exception as e:
        logger.error(f"更新降噪配置失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))
