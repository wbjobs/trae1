from fastapi import APIRouter, HTTPException, Query
from typing import Optional, List
from datetime import datetime, timedelta
from models.schemas import (
    QueryParams,
    TrendData,
    ErrorSummary,
    ErrorDetail
)
from services.query_service import QueryService

router = APIRouter(prefix="/api/query", tags=["Data Query"])


@router.post("/performance/trends")
async def get_performance_trends(params: QueryParams):
    try:
        trends = QueryService.get_all_performance_trends(params)
        return {"status": "success", "data": trends}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/performance/trend/{metric}")
async def get_performance_trend(metric: str, params: QueryParams):
    valid_metrics = ["fp", "fcp", "lcp", "ttfb", "dom_ready", "load_time"]
    if metric not in valid_metrics:
        raise HTTPException(
            status_code=400,
            detail=f"Invalid metric. Valid metrics: {', '.join(valid_metrics)}"
        )

    try:
        trend = QueryService.get_performance_trend(params, metric)
        return {"status": "success", "data": trend}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/errors/summary")
async def get_error_summary(params: QueryParams):
    try:
        summary = QueryService.get_error_summary(params)
        return {"status": "success", "data": summary}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/errors/details")
async def get_error_details(
    params: QueryParams,
    error_type: Optional[str] = Query(None, description="Error type filter"),
    limit: int = Query(100, ge=1, le=1000, description="Number of records"),
    offset: int = Query(0, ge=0, description="Offset for pagination")
):
    try:
        details = QueryService.get_error_details(params, error_type, limit, offset)
        return {"status": "success", "data": details}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/renderer/summary")
async def get_renderer_summary(params: QueryParams):
    try:
        summary = QueryService.get_renderer_summary(params)
        return {"status": "success", "data": summary}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/stats")
async def get_app_stats(params: QueryParams):
    try:
        stats = QueryService.get_app_stats(params)
        return {"status": "success", "data": stats}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
