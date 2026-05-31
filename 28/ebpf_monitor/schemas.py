"""Pydantic schemas for API requests/responses."""
from datetime import datetime
from typing import List, Optional

from pydantic import BaseModel, Field


class SyscallEventResponse(BaseModel):
    id: int
    pid: int
    comm: str
    syscall_name: str
    timestamp: datetime
    duration_ns: Optional[int]
    return_value: Optional[int]
    file_path: Optional[str]
    extra_args: Optional[str]

    class Config:
        orm_mode = True
        from_attributes = True


class SyscallStatPerSyscall(BaseModel):
    syscall_name: str
    count: int
    avg_duration_ns: Optional[float]


class TopFile(BaseModel):
    file_path: Optional[str]
    count: int


class SyscallStatsResponse(BaseModel):
    pid: int
    start: datetime
    end: datetime
    total_calls: int
    per_syscall: List[SyscallStatPerSyscall]
    top_files: List[TopFile]


class SyscallListResponse(BaseModel):
    items: List[SyscallEventResponse]
    total: int
    pid: int
    start: datetime
    end: datetime


class MonitorStartRequest(BaseModel):
    pid: int = Field(..., gt=0, description="Target PID to monitor")


class AlertResponse(BaseModel):
    id: int
    pid: int
    comm: str
    syscall_name: str
    timestamp: datetime
    duration_ns: int
    threshold_ns: int
    file_path: Optional[str]
    message: str

    class Config:
        from_attributes = True


class AlertListResponse(BaseModel):
    items: List[AlertResponse]
    total: int
