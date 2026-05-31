"""FastAPI application entrypoint."""
from __future__ import annotations

import asyncio
import logging
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import List, Optional

from fastapi import Depends, FastAPI, HTTPException, Query, WebSocket, status
from fastapi.responses import FileResponse, HTMLResponse
from sqlalchemy import desc, func
from sqlalchemy.orm import Session

from .alerts import get_alert_manager
from .config import settings
from .database import Base, engine, get_db
from .manager import get_monitor_manager
from .models import Alert as AlertModel
from .queries import count_total_calls, list_events, stats_per_syscall, top_files
from .schemas import (
    AlertListResponse,
    AlertResponse,
    MonitorStartRequest,
    SyscallListResponse,
    SyscallStatsResponse,
)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)


STATIC_DIR = Path(__file__).resolve().parent.parent / "static"


@asynccontextmanager
async def lifespan(app: FastAPI):  # noqa: ARG001
    Base.metadata.create_all(bind=engine)
    # Give the alert manager a reference to the running event loop so
    # alerts dispatched from background threads can be scheduled on it.
    try:
        loop = asyncio.get_running_loop()
        get_alert_manager().set_event_loop(loop)
    except RuntimeError:
        pass
    try:
        yield
    finally:
        try:
            get_monitor_manager().stop_all()
        except Exception:
            pass


app = FastAPI(
    title="eBPF Syscall Monitor API",
    description=(
        "Monitor syscalls (openat/read/write/execve) via eBPF, persist them "
        "to PostgreSQL, and emit slow-syscall alerts over WebSocket."
    ),
    version="0.2.0",
    lifespan=lifespan,
)


# ---------------------------------------------------------------------------
# Static dashboard
# ---------------------------------------------------------------------------
@app.get("/dashboard", response_class=HTMLResponse, include_in_schema=False)
def dashboard():
    index = STATIC_DIR / "index.html"
    if index.exists():
        return FileResponse(index)
    return HTMLResponse(
        "<h1>Dashboard not installed</h1>"
        "<p>Create <code>static/index.html</code> in the project root.</p>",
        status_code=503,
    )


@app.get("/favicon.ico", include_in_schema=False)
def favicon():
    return HTMLResponse("")


# ---------------------------------------------------------------------------
# Info
# ---------------------------------------------------------------------------
@app.get("/", tags=["info"])
def root():
    return {
        "service": "ebpf-syscall-monitor",
        "version": "0.2.0",
        "api_port": settings.api_port,
        "slow_threshold_ms": settings.slow_threshold_ms,
        "dashboard": "/dashboard",
    }


# ---------------------------------------------------------------------------
# Monitor management
# ---------------------------------------------------------------------------
@app.post("/monitors", tags=["monitor"], status_code=status.HTTP_201_CREATED)
def start_monitor(req: MonitorStartRequest):
    mgr = get_monitor_manager()
    if mgr.is_running(req.pid):
        return {"status": "already_running", "pid": req.pid}
    try:
        started = mgr.start(req.pid)
    except RuntimeError as e:
        raise HTTPException(status_code=500, detail=f"Failed to start monitor: {e}")
    if not started:
        raise HTTPException(status_code=409, detail=f"PID {req.pid} already monitored")
    return {"status": "started", "pid": req.pid}


@app.delete("/monitors/{pid}", tags=["monitor"])
def stop_monitor(pid: int):
    mgr = get_monitor_manager()
    if not mgr.stop(pid):
        raise HTTPException(status_code=404, detail=f"No monitor for PID {pid}")
    return {"status": "stopped", "pid": pid}


@app.get("/monitors", tags=["monitor"])
def list_monitors():
    return {"monitors": get_monitor_manager().status()}


# ---------------------------------------------------------------------------
# Syscall query
# ---------------------------------------------------------------------------
@app.get(
    "/processes/{pid}/syscalls/stats",
    tags=["query"],
    response_model=SyscallStatsResponse,
)
def process_stats(
    pid: int,
    start: Optional[datetime] = Query(
        None, description="Start UTC datetime (inclusive). Defaults to last 1 hour."
    ),
    end: Optional[datetime] = Query(
        None, description="End UTC datetime (inclusive). Defaults to now."
    ),
    db: Session = Depends(get_db),
):
    end = end or datetime.now(tz=timezone.utc)
    start = start or (end - timedelta(hours=1))
    total = count_total_calls(db, pid, start, end)
    per = stats_per_syscall(db, pid, start, end)
    tops = top_files(db, pid, start, end, limit=5)
    return SyscallStatsResponse(
        pid=pid,
        start=start,
        end=end,
        total_calls=total,
        per_syscall=per,
        top_files=tops,
    )


@app.get(
    "/processes/{pid}/syscalls",
    tags=["query"],
    response_model=SyscallListResponse,
)
def process_events(
    pid: int,
    start: Optional[datetime] = Query(None),
    end: Optional[datetime] = Query(None),
    syscall_name: str = Query(""),
    offset: int = Query(0, ge=0),
    limit: int = Query(100, ge=1, le=1000),
    db: Session = Depends(get_db),
):
    end = end or datetime.now(tz=timezone.utc)
    start = start or (end - timedelta(hours=1))
    rows, total = list_events(
        db, pid, start, end, offset=offset, limit=limit, syscall_name=syscall_name
    )
    return SyscallListResponse(
        items=[
            {
                "id": r.id,
                "pid": r.pid,
                "comm": r.comm,
                "syscall_name": r.syscall_name,
                "timestamp": r.timestamp,
                "duration_ns": r.duration_ns,
                "return_value": r.return_value,
                "file_path": r.file_path,
                "extra_args": r.extra_args,
            }
            for r in rows
        ],
        total=total,
        pid=pid,
        start=start,
        end=end,
    )


# ---------------------------------------------------------------------------
# Alerts
# ---------------------------------------------------------------------------
@app.get("/alerts", tags=["alerts"], response_model=AlertListResponse)
def list_alerts(
    pid: Optional[int] = Query(None, description="Filter by PID"),
    limit: int = Query(100, ge=1, le=1000),
    offset: int = Query(0, ge=0),
    start: Optional[datetime] = Query(None),
    end: Optional[datetime] = Query(None),
    db: Session = Depends(get_db),
):
    q = db.query(AlertModel)
    if pid is not None:
        q = q.filter(AlertModel.pid == pid)
    if start is not None:
        q = q.filter(AlertModel.timestamp >= start)
    if end is not None:
        q = q.filter(AlertModel.timestamp <= end)
    total = int(db.query(func.count()).select_from(q.subquery()).scalar() or 0)
    rows: List[AlertModel] = (
        q.order_by(desc(AlertModel.timestamp)).offset(offset).limit(limit).all()
    )
    return AlertListResponse(
        items=[
            AlertResponse(
                id=r.id,
                pid=r.pid,
                comm=r.comm,
                syscall_name=r.syscall_name,
                timestamp=r.timestamp,
                duration_ns=r.duration_ns,
                threshold_ns=r.threshold_ns,
                file_path=r.file_path,
                message=r.message,
            )
            for r in rows
        ],
        total=total,
    )


@app.get("/alerts/recent", tags=["alerts"])
def recent_alerts(limit: int = Query(50, ge=1, le=500)):
    mgr = get_alert_manager()
    items = mgr.recent_alerts(limit=limit)
    return {
        "threshold_ns": mgr.threshold_ns,
        "threshold_ms": settings.slow_threshold_ms,
        "count": mgr.count(),
        "items": items,
    }


@app.websocket("/ws/alerts")
async def ws_alerts(websocket: WebSocket):
    await websocket.accept()
    await get_alert_manager().add_client(websocket)
