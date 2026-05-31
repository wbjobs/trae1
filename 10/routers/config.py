import logging
from fastapi import APIRouter, HTTPException
from typing import List, Dict, Any

from config import settings, get_threshold_manager
from models.schemas import ThresholdUpdate, ThresholdInfo

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/config", tags=["配置管理"])


@router.get("/thresholds", summary="获取所有阈值配置")
async def get_all_thresholds():
    try:
        tm = get_threshold_manager()
        thresholds = tm.get_all_thresholds()
        return {
            "success": True,
            "data": {
                "thresholds": thresholds,
                "overrides": {k: v for k, v in thresholds.items() if k in tm._overrides}
            }
        }
    except Exception as e:
        logger.error(f"获取阈值配置失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/thresholds/{name}", summary="获取单个阈值配置")
async def get_threshold(name: str):
    try:
        tm = get_threshold_manager()
        default_value = getattr(settings, name, None)
        current_value = tm.get_threshold(name, default_value)

        if current_value is None:
            raise HTTPException(status_code=404, detail=f"阈值配置不存在: {name}")

        return {
            "success": True,
            "data": {
                "name": name,
                "current_value": current_value,
                "default_value": default_value,
                "is_overridden": name in tm._overrides
            }
        }
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"获取阈值配置失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.put("/thresholds/{name}", summary="更新阈值配置")
async def update_threshold(name: str, value: float):
    try:
        tm = get_threshold_manager()
        success = tm.set_threshold(name, value)

        if not success:
            raise HTTPException(status_code=400, detail=f"无法设置阈值: {name}")

        logger.info(f"阈值配置已更新: {name} = {value}")
        return {
            "success": True,
            "message": f"阈值 {name} 已更新为 {value}",
            "data": {"name": name, "value": value}
        }
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"更新阈值配置失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.put("/thresholds", summary="批量更新阈值配置")
async def batch_update_thresholds(updates: List[ThresholdUpdate]):
    try:
        tm = get_threshold_manager()
        results = []
        for update in updates:
            success = tm.set_threshold(update.name, update.value)
            results.append({
                "name": update.name,
                "value": update.value,
                "success": success
            })

        success_count = len([r for r in results if r["success"]])
        return {
            "success": True,
            "message": f"成功更新 {success_count}/{len(updates)} 个阈值配置",
            "data": results
        }
    except Exception as e:
        logger.error(f"批量更新阈值配置失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.delete("/thresholds/{name}", summary="重置阈值为默认值")
async def reset_threshold(name: str):
    try:
        tm = get_threshold_manager()
        if tm.reset_threshold(name):
            return {
                "success": True,
                "message": f"阈值 {name} 已重置为默认值",
                "data": {"name": name, "default_value": getattr(settings, name, None)}
            }
        else:
            raise HTTPException(status_code=404, detail=f"阈值配置不存在或未覆盖: {name}")
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"重置阈值配置失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.delete("/thresholds", summary="重置所有阈值为默认值")
async def reset_all_thresholds():
    try:
        tm = get_threshold_manager()
        tm.reset_all()
        return {
            "success": True,
            "message": "所有阈值配置已重置为默认值"
        }
    except Exception as e:
        logger.error(f"重置所有阈值配置失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/system", summary="获取系统配置")
async def get_system_config():
    return {
        "success": True,
        "data": {
            "app_name": settings.APP_NAME,
            "app_version": settings.APP_VERSION,
            "debug": settings.DEBUG,
            "collect_interval": settings.COLLECT_INTERVAL,
            "metrics_retention_days": settings.METRICS_RETENTION_DAYS,
            "max_concurrent_collections": settings.MAX_CONCURRENT_COLLECTIONS,
            "remote_collect_timeout": settings.REMOTE_COLLECT_TIMEOUT,
            "ranking_top_n": settings.RANKING_TOP_N,
            "ranking_time_window_minutes": settings.RANKING_TIME_WINDOW_MINUTES,
            "influxdb_url": settings.INFLUXDB_URL,
            "influxdb_org": settings.INFLUXDB_ORG,
            "influxdb_bucket": settings.INFLUXDB_BUCKET
        }
    }
