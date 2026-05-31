from typing import List, Optional, Dict, Any
from fastapi import APIRouter, File, UploadFile, HTTPException, Query
from pydantic import BaseModel
from app.scanner.engine import ScanEngine
from app.config import settings

router = APIRouter(prefix="/api/v1", tags=["scanner"])

scan_engine = None


def get_scan_engine() -> ScanEngine:
    global scan_engine
    if scan_engine is None:
        scan_engine = ScanEngine()
    return scan_engine


class HeuristicFinding(BaseModel):
    type: str
    name: str
    description: str
    severity: str
    offset: Optional[int] = None
    details: Optional[str] = None


class HeuristicScanResult(BaseModel):
    enabled: bool = True
    findings: List[HeuristicFinding] = []
    total_findings: int = 0
    highest_severity: str = "clean"
    entropy: float = 0.0
    entropy_category: str = "normal"


class ScanResponse(BaseModel):
    file_md5: str
    file_name: Optional[str] = None
    file_size: Optional[int] = None
    matched_rules: list = []
    scan_time: Optional[str] = None
    total_matches: int = 0
    highest_severity: str = "clean"
    cached: bool = False
    error: Optional[str] = None
    heuristic_scan: Optional[Dict[str, Any]] = None


class BatchScanResponse(BaseModel):
    results: List[dict]
    total_files: int
    scanned_count: int
    cached_count: int
    malicious_count: int


class RuleReloadRequest(BaseModel):
    force: bool = False


@router.post("/scan", response_model=ScanResponse)
async def scan_file(file: UploadFile = File(...)):
    if not file.filename:
        raise HTTPException(status_code=400, detail="No file provided")

    file_data = await file.read()
    file_size = len(file_data)

    if file_size > settings.max_file_size_mb * 1024 * 1024:
        raise HTTPException(
            status_code=413,
            detail=f"File too large. Maximum size is {settings.max_file_size_mb}MB",
        )

    engine = get_scan_engine()

    try:
        result = engine.scan_file(file.filename, file_data)
        return ScanResponse(**result)
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/scan/batch", response_model=BatchScanResponse)
async def scan_files(files: List[UploadFile] = File(...)):
    if len(files) > settings.max_parallel_files:
        raise HTTPException(
            status_code=400,
            detail=f"Too many files. Maximum is {settings.max_parallel_files}",
        )

    engine = get_scan_engine()

    file_data_list = []
    for file in files:
        if not file.filename:
            continue
        data = await file.read()
        if len(data) > settings.max_file_size_mb * 1024 * 1024:
            continue
        file_data_list.append((file.filename, data))

    results = engine.scan_files(file_data_list)

    cached_count = sum(1 for r in results if r.get("cached", False))
    malicious_count = sum(
        1 for r in results if r.get("highest_severity", "clean") in ["high", "critical"]
    )

    return BatchScanResponse(
        results=results,
        total_files=len(files),
        scanned_count=len(results),
        cached_count=cached_count,
        malicious_count=malicious_count,
    )


@router.get("/scan/result/{file_md5}", response_model=ScanResponse)
async def get_scan_result(file_md5: str):
    if len(file_md5) != 32:
        raise HTTPException(status_code=400, detail="Invalid MD5 hash format")

    engine = get_scan_engine()
    result = engine.get_result_by_md5(file_md5)

    if not result:
        raise HTTPException(status_code=404, detail="Scan result not found")

    return ScanResponse(**result)


@router.get("/scan/history")
async def get_scan_history(
    skip: int = Query(0, ge=0),
    limit: int = Query(100, ge=1, le=1000),
    severity: Optional[str] = Query(None, pattern="^(clean|low|medium|high|critical)$"),
):
    engine = get_scan_engine()
    results = engine.get_scan_history(skip=skip, limit=limit, severity=severity)
    return {"results": results, "total": len(results)}


@router.get("/stats")
async def get_scan_stats():
    engine = get_scan_engine()
    return engine.get_scan_stats()


@router.get("/health")
async def health_check():
    engine = get_scan_engine()
    stats = engine.get_scan_stats()
    rules_status = engine.get_rules_status()
    return {
        "status": "healthy",
        "rules_loaded": stats["rules_count"],
        "hot_reload_enabled": rules_status["hot_reload_enabled"],
        "heuristic_scan_enabled": rules_status.get("heuristic_scan_enabled", False),
    }


@router.post("/rules/reload")
async def reload_rules(request: RuleReloadRequest = RuleReloadRequest()):
    engine = get_scan_engine()
    result = engine.reload_rules(force=request.force)
    return result


@router.get("/rules/status")
async def get_rules_status():
    engine = get_scan_engine()
    return engine.get_rules_status()


@router.get("/rules/list")
async def list_rules(
    category: Optional[str] = None,
    severity: Optional[str] = Query(None, pattern="^(clean|low|medium|high|critical)$"),
):
    engine = get_scan_engine()
    metadata = engine.rule_loader.get_all_rules_metadata()

    rules = []
    for name, meta in metadata.items():
        if category and meta.category != category:
            continue
        if severity and meta.severity != severity:
            continue
        rules.append({
            "name": meta.name,
            "description": meta.description,
            "severity": meta.severity,
            "category": meta.category,
        })

    return {
        "total": len(rules),
        "rules": sorted(rules, key=lambda r: (r["category"], r["name"])),
    }
