from pydantic import BaseModel, Field
from typing import Optional, List
from datetime import datetime


class QueryParams(BaseModel):
    username: Optional[str] = None
    start_time: Optional[float] = None
    end_time: Optional[float] = None
    extensions: Optional[List[str]] = None
    operation_type: Optional[str] = None
    file_path: Optional[str] = None
    source_ip: Optional[str] = None
    size: int = Field(default=100, ge=1, le=10000)
    scroll_id: Optional[str] = None


class EventResponse(BaseModel):
    id: str
    operation_type: str
    file_path: str
    old_file_path: Optional[str] = None
    timestamp: float
    username: str
    source_ip: str
    file_size: int
    file_extension: str
    iso_timestamp: str


class QueryResponse(BaseModel):
    total: int
    events: List[EventResponse]
    scroll_id: Optional[str] = None


class TopUser(BaseModel):
    username: str
    count: int


class TrendEntry(BaseModel):
    date: str
    total: int
    by_operation: dict


class ExtensionStat(BaseModel):
    extension: str
    count: int


class DashboardResponse(BaseModel):
    top_users: List[TopUser]
    operation_trend: List[TrendEntry]
    extension_stats: List[ExtensionStat]


class HealthResponse(BaseModel):
    status: str
    elasticsearch: bool
    smb_connected: bool
    uptime_seconds: float
