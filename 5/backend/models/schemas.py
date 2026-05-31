from pydantic import BaseModel, Field
from typing import Optional, List, Dict, Any
from datetime import datetime


class PerformanceMetrics(BaseModel):
    fp: Optional[float] = Field(None, description="First Paint 首次绘制时间(ms)")
    fcp: Optional[float] = Field(None, description="First Contentful Paint 首次内容绘制时间(ms)")
    lcp: Optional[float] = Field(None, description="Largest Contentful Paint 最大内容绘制时间(ms)")
    ttfb: Optional[float] = Field(None, description="Time To First Byte 首字节时间(ms)")
    dom_ready: Optional[float] = Field(None, description="DOM Ready 时间(ms)")
    load_time: Optional[float] = Field(None, description="页面完全加载时间(ms)")


class ErrorInfo(BaseModel):
    type: str = Field(..., description="错误类型")
    message: str = Field(..., description="错误信息")
    stack: Optional[str] = Field(None, description="错误堆栈")
    filename: Optional[str] = Field(None, description="出错文件")
    lineno: Optional[int] = Field(None, description="出错行号")
    colno: Optional[int] = Field(None, description="出错列号")
    error_type: str = Field(..., description="错误分类: js/resource/http")


class RendererInfo(BaseModel):
    fps: float = Field(..., description="FPS 帧率")
    memory_used: Optional[float] = Field(None, description="内存使用(MB)")
    long_task_count: int = Field(0, description="长任务数量")
    jank_count: int = Field(0, description="卡顿次数")


class BaseReport(BaseModel):
    app_id: str = Field(..., description="应用标识")
    user_id: Optional[str] = Field(None, description="用户标识")
    session_id: str = Field(..., description="会话标识")
    page_url: str = Field(..., description="页面URL")
    user_agent: Optional[str] = Field(None, description="用户代理")
    timestamp: datetime = Field(default_factory=datetime.now, description="上报时间")


class PerformanceReport(BaseReport):
    metrics: PerformanceMetrics


class ErrorReport(BaseReport):
    error: ErrorInfo


class RendererReport(BaseReport):
    renderer: RendererInfo


class BatchReport(BaseModel):
    performance: Optional[List[PerformanceReport]] = None
    errors: Optional[List[ErrorReport]] = None
    renderer: Optional[List[RendererReport]] = None


class QueryParams(BaseModel):
    app_id: str
    start_time: datetime
    end_time: datetime
    page_url: Optional[str] = None
    user_id: Optional[str] = None


class TrendPoint(BaseModel):
    time: datetime
    value: float


class TrendData(BaseModel):
    metric: str
    points: List[TrendPoint]
    avg: float
    min: float
    max: float
    p50: float
    p95: float


class ErrorSummary(BaseModel):
    error_type: str
    count: int
    last_occurrence: datetime
    messages: List[str]


class ErrorDetail(BaseModel):
    id: str
    timestamp: datetime
    error_type: str
    message: str
    stack: Optional[str]
    filename: Optional[str]
    lineno: Optional[int]
    colno: Optional[int]
    page_url: str
    user_id: Optional[str]
