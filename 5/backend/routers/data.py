import logging
from fastapi import APIRouter, HTTPException, Request
from typing import List, Optional
from models.schemas import (
    PerformanceReport,
    ErrorReport,
    RendererReport,
    BatchReport
)
from services.data_service import DataReportService

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/report", tags=["Data Report"])


@router.post("/performance")
async def report_performance(report: PerformanceReport):
    try:
        result = DataReportService.report_performance(report)
        if result["status"] == "error":
            raise HTTPException(status_code=400, detail=result["message"])
        return result
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"Performance report error: {e}")
        raise HTTPException(status_code=500, detail=f"Internal error: {str(e)}")


@router.post("/error")
async def report_error(report: ErrorReport):
    try:
        result = DataReportService.report_error(report)
        if result["status"] == "error":
            raise HTTPException(status_code=400, detail=result["message"])
        return result
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"Error report error: {e}")
        raise HTTPException(status_code=500, detail=f"Internal error: {str(e)}")


@router.post("/renderer")
async def report_renderer(report: RendererReport):
    try:
        result = DataReportService.report_renderer(report)
        if result["status"] == "error":
            raise HTTPException(status_code=400, detail=result["message"])
        return result
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"Renderer report error: {e}")
        raise HTTPException(status_code=500, detail=f"Internal error: {str(e)}")


@router.post("/batch")
async def report_batch(batch: BatchReport):
    try:
        results = {}

        if batch.performance:
            performance_data = [p.model_dump() if hasattr(p, 'model_dump') else p.dict() for p in batch.performance]
            results["performance"] = DataReportService.report_batch(
                performance_data, "performance"
            )

        if batch.errors:
            errors_data = [e.model_dump() if hasattr(e, 'model_dump') else e.dict() for e in batch.errors]
            results["errors"] = DataReportService.report_batch(
                errors_data, "errors"
            )

        if batch.renderer:
            renderer_data = [r.model_dump() if hasattr(r, 'model_dump') else r.dict() for r in batch.renderer]
            results["renderer"] = DataReportService.report_batch(
                renderer_data, "renderer"
            )

        return {"status": "success", "results": results}
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"Batch report error: {e}")
        raise HTTPException(status_code=500, detail=f"Internal error: {str(e)}")


@router.post("/raw")
async def report_raw(request: Request):
    try:
        data = await request.json()

        report_type = data.get("type", "performance")
        report_data = data.get("data", data)

        if isinstance(report_data, list):
            result = DataReportService.report_batch(report_data, report_type)
        else:
            result = DataReportService.report_batch([report_data], report_type)

        return result
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"Raw report error: {e}")
        raise HTTPException(status_code=500, detail=f"Internal error: {str(e)}")
