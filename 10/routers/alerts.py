import logging
from fastapi import APIRouter, HTTPException, Query
from datetime import datetime, timedelta
from typing import Optional, List

from models.schemas import AlertQuery, AlertResponse
from db.influxdb_client import get_influxdb_manager

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/alerts", tags=["告警管理"])


@router.get("", summary="查询告警记录")
async def get_alerts(
    hostname: Optional[str] = Query(None, description="主机名"),
    metric_type: Optional[str] = Query(None, description="指标类型: cpu/memory/disk/network"),
    severity: Optional[str] = Query(None, description="告警级别: info/warning/critical"),
    status: Optional[str] = Query("active", description="状态: active/acknowledged/resolved"),
    start_time: Optional[datetime] = Query(None, description="开始时间"),
    end_time: Optional[datetime] = Query(None, description="结束时间")
):
    try:
        db = get_influxdb_manager()

        if not start_time:
            start_time = datetime.utcnow() - timedelta(days=7)
        if not end_time:
            end_time = datetime.utcnow()

        filters = {}
        if hostname:
            filters["hostname"] = hostname
        if metric_type:
            filters["metric_type"] = metric_type
        if severity:
            filters["severity"] = severity
        if status:
            filters["status"] = status

        results = db.query_alerts(
            start_time=start_time,
            end_time=end_time,
            filters=filters if filters else None
        )

        return AlertResponse(
            success=True,
            message="查询告警记录成功",
            data={
                "total": len(results),
                "alerts": results,
                "filters": filters
            }
        )
    except Exception as e:
        logger.error(f"查询告警记录失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/active", summary="获取活跃告警")
async def get_active_alerts(
    hostname: Optional[str] = Query(None, description="主机名")
):
    try:
        db = get_influxdb_manager()

        filters = {"status": "active"}
        if hostname:
            filters["hostname"] = hostname

        results = db.query_alerts(
            start_time=datetime.utcnow() - timedelta(days=30),
            end_time=datetime.utcnow(),
            filters=filters
        )

        critical_count = len([a for a in results if a.get("severity") == "critical"])
        warning_count = len([a for a in results if a.get("severity") == "warning"])

        return {
            "success": True,
            "message": "获取活跃告警成功",
            "data": {
                "total": len(results),
                "critical_count": critical_count,
                "warning_count": warning_count,
                "alerts": results
            }
        }
    except Exception as e:
        logger.error(f"获取活跃告警失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/{alert_id}/acknowledge", summary="确认告警")
async def acknowledge_alert(
    alert_id: str,
    acknowledged_by: str = Query(..., description="确认人")
):
    try:
        db = get_influxdb_manager()

        alert_data = {
            "alert_id": alert_id,
            "status": "acknowledged",
            "acknowledged_by": acknowledged_by,
            "acknowledged_at": datetime.utcnow().isoformat()
        }

        db.write_alert(alert_data)

        return AlertResponse(
            success=True,
            message=f"告警 {alert_id} 已确认",
            data={"alert_id": alert_id, "acknowledged_by": acknowledged_by}
        )
    except Exception as e:
        logger.error(f"确认告警失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/{alert_id}/resolve", summary="解决告警")
async def resolve_alert(alert_id: str):
    try:
        db = get_influxdb_manager()

        alert_data = {
            "alert_id": alert_id,
            "status": "resolved",
            "resolved_at": datetime.utcnow().isoformat()
        }

        db.write_alert(alert_data)

        return AlertResponse(
            success=True,
            message=f"告警 {alert_id} 已解决",
            data={"alert_id": alert_id}
        )
    except Exception as e:
        logger.error(f"解决告警失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/statistics", summary="告警统计")
async def alert_statistics(
    hostname: Optional[str] = Query(None, description="主机名"),
    days: int = Query(7, description="统计天数")
):
    try:
        db = get_influxdb_manager()
        end_time = datetime.utcnow()
        start_time = end_time - timedelta(days=days)

        filters = {}
        if hostname:
            filters["hostname"] = hostname

        results = db.query_alerts(
            start_time=start_time,
            end_time=end_time,
            filters=filters if filters else None
        )

        stats = {
            "total_alerts": len(results),
            "by_severity": {
                "critical": len([a for a in results if a.get("severity") == "critical"]),
                "warning": len([a for a in results if a.get("severity") == "warning"]),
                "info": len([a for a in results if a.get("severity") == "info"])
            },
            "by_type": {},
            "by_status": {
                "active": len([a for a in results if a.get("status") == "active"]),
                "acknowledged": len([a for a in results if a.get("status") == "acknowledged"]),
                "resolved": len([a for a in results if a.get("status") == "resolved"])
            },
            "period": f"{days}天"
        }

        for alert in results:
            metric_type = alert.get("metric_type", "unknown")
            if metric_type not in stats["by_type"]:
                stats["by_type"][metric_type] = 0
            stats["by_type"][metric_type] += 1

        return {
            "success": True,
            "message": "告警统计完成",
            "data": stats
        }
    except Exception as e:
        logger.error(f"告警统计失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))
