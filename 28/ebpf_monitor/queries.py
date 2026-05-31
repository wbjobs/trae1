"""Query helpers for syscall statistics."""
from __future__ import annotations

from datetime import datetime
from typing import List, Tuple

from sqlalchemy import and_, func
from sqlalchemy.orm import Session

from .models import SyscallEvent as SyscallEventModel
from .schemas import SyscallStatPerSyscall, TopFile


def _apply_time_filter(query, pid: int, start: datetime, end: datetime):
    return query.filter(
        and_(
            SyscallEventModel.pid == pid,
            SyscallEventModel.timestamp >= start,
            SyscallEventModel.timestamp <= end,
        )
    )


def count_total_calls(session: Session, pid: int, start: datetime, end: datetime) -> int:
    q = session.query(func.count(SyscallEventModel.id))
    q = _apply_time_filter(q, pid, start, end)
    return int(q.scalar() or 0)


def stats_per_syscall(
    session: Session, pid: int, start: datetime, end: datetime
) -> List[SyscallStatPerSyscall]:
    q = session.query(
        SyscallEventModel.syscall_name,
        func.count(SyscallEventModel.id).label("c"),
        func.avg(SyscallEventModel.duration_ns).label("avg"),
    )
    q = _apply_time_filter(q, pid, start, end)
    q = q.group_by(SyscallEventModel.syscall_name).order_by(SyscallEventModel.syscall_name)
    result: List[SyscallStatPerSyscall] = []
    for name, count, avg in q.all():
        result.append(
            SyscallStatPerSyscall(
                syscall_name=name or "",
                count=int(count or 0),
                avg_duration_ns=float(avg) if avg is not None else None,
            )
        )
    return result


def top_files(
    session: Session, pid: int, start: datetime, end: datetime, limit: int = 5
) -> List[TopFile]:
    q = session.query(
        SyscallEventModel.file_path,
        func.count(SyscallEventModel.id).label("c"),
    )
    q = _apply_time_filter(q, pid, start, end)
    q = q.filter(SyscallEventModel.file_path.isnot(None))
    q = q.group_by(SyscallEventModel.file_path).order_by(func.count(SyscallEventModel.id).desc())
    q = q.limit(limit)
    return [TopFile(file_path=fp, count=int(c or 0)) for fp, c in q.all()]


def list_events(
    session: Session,
    pid: int,
    start: datetime,
    end: datetime,
    offset: int = 0,
    limit: int = 100,
    syscall_name: str = "",
) -> Tuple[List[SyscallEventModel], int]:
    q = session.query(SyscallEventModel)
    q = _apply_time_filter(q, pid, start, end)
    if syscall_name:
        q = q.filter(SyscallEventModel.syscall_name == syscall_name)
    total = int(session.query(func.count()).select_from(q.subquery()).scalar() or 0)
    q = q.order_by(SyscallEventModel.timestamp.desc()).offset(offset).limit(limit)
    return q.all(), total
