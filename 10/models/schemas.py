from datetime import datetime
from typing import Optional, List, Dict, Any
from pydantic import BaseModel, Field


class CPUMetrics(BaseModel):
    usage_percent: float = Field(..., description="CPU使用率百分比")
    user_percent: float = Field(default=0.0, description="用户态CPU使用率")
    system_percent: float = Field(default=0.0, description="系统态CPU使用率")
    idle_percent: float = Field(default=0.0, description="空闲CPU使用率")
    iowait_percent: float = Field(default=0.0, description="IO等待CPU使用率")
    load_avg_1m: float = Field(default=0.0, description="1分钟平均负载")
    load_avg_5m: float = Field(default=0.0, description="5分钟平均负载")
    load_avg_15m: float = Field(default=0.0, description="15分钟平均负载")
    timestamp: datetime = Field(default_factory=datetime.utcnow, description="采集时间戳")


class MemoryMetrics(BaseModel):
    total: int = Field(..., description="总内存(字节)")
    available: int = Field(..., description="可用内存(字节)")
    used: int = Field(..., description="已用内存(字节)")
    usage_percent: float = Field(..., description="内存使用率百分比")
    buffers: int = Field(default=0, description="缓冲区内存(字节)")
    cached: int = Field(default=0, description="缓存内存(字节)")
    shared: int = Field(default=0, description="共享内存(字节)")
    swap_total: int = Field(default=0, description="交换分区总量(字节)")
    swap_used: int = Field(default=0, description="已用交换分区(字节)")
    swap_percent: float = Field(default=0.0, description="交换分区使用率")
    timestamp: datetime = Field(default_factory=datetime.utcnow, description="采集时间戳")


class DiskMetrics(BaseModel):
    device: str = Field(..., description="磁盘设备名")
    mount_point: str = Field(..., description="挂载点")
    total: int = Field(..., description="总容量(字节)")
    used: int = Field(..., description="已用容量(字节)")
    free: int = Field(..., description="可用容量(字节)")
    usage_percent: float = Field(..., description="磁盘使用率百分比")
    read_bytes: int = Field(default=0, description="读取字节数")
    write_bytes: int = Field(default=0, description="写入字节数")
    read_count: int = Field(default=0, description="读取次数")
    write_count: int = Field(default=0, description="写入次数")
    timestamp: datetime = Field(default_factory=datetime.utcnow, description="采集时间戳")


class NetworkMetrics(BaseModel):
    interface: str = Field(..., description="网络接口名")
    bytes_sent: int = Field(..., description="发送字节数")
    bytes_recv: int = Field(..., description="接收字节数")
    bytes_sent_rate: float = Field(default=0.0, description="发送速率(字节/秒)")
    bytes_recv_rate: float = Field(default=0.0, description="接收速率(字节/秒)")
    packets_sent: int = Field(default=0, description="发送数据包数")
    packets_recv: int = Field(default=0, description="接收数据包数")
    errors_in: int = Field(default=0, description="接收错误数")
    errors_out: int = Field(default=0, description="发送错误数")
    errors_in_rate: float = Field(default=0.0, description="接收错误速率")
    errors_out_rate: float = Field(default=0.0, description="发送错误速率")
    drops_in: int = Field(default=0, description="接收丢包数")
    drops_out: int = Field(default=0, description="发送丢包数")
    drops_in_rate: float = Field(default=0.0, description="接收丢包速率")
    drops_out_rate: float = Field(default=0.0, description="发送丢包速率")
    timestamp: datetime = Field(default_factory=datetime.utcnow, description="采集时间戳")


class ServerMetrics(BaseModel):
    hostname: str = Field(..., description="主机名")
    ip_address: str = Field(default="", description="IP地址")
    cpu: Optional[CPUMetrics] = None
    memory: Optional[MemoryMetrics] = None
    disks: List[DiskMetrics] = Field(default_factory=list)
    networks: List[NetworkMetrics] = Field(default_factory=list)
    timestamp: datetime = Field(default_factory=datetime.utcnow, description="采集时间戳")


class AlertRecord(BaseModel):
    id: Optional[str] = None
    hostname: str
    metric_type: str
    metric_name: str
    threshold: float
    current_value: float
    severity: str = Field(default="warning", description="告警级别: info/warning/critical")
    message: str
    status: str = Field(default="active", description="状态: active/acknowledged/resolved")
    acknowledged_by: Optional[str] = None
    acknowledged_at: Optional[datetime] = None
    resolved_at: Optional[datetime] = None
    created_at: datetime = Field(default_factory=datetime.utcnow)


class AlertQuery(BaseModel):
    hostname: Optional[str] = None
    metric_type: Optional[str] = None
    severity: Optional[str] = None
    status: Optional[str] = None
    start_time: Optional[datetime] = None
    end_time: Optional[datetime] = None


class MetricsQuery(BaseModel):
    hostname: str
    metric_type: str
    metric_name: Optional[str] = None
    start_time: datetime
    end_time: datetime
    aggregation: Optional[str] = Field(default=None, description="聚合方式: mean/max/min/sum/count")
    interval: Optional[str] = Field(default=None, description="聚合间隔: 1m/5m/1h")


class TrendAnalysisResult(BaseModel):
    hostname: str
    metric_type: str
    metric_name: str
    current_value: float
    average_value: float
    max_value: float
    min_value: float
    trend_direction: str = Field(description="趋势方向: increasing/decreasing/stable")
    trend_rate: float = Field(description="趋势变化率(%)")
    prediction_24h: Optional[float] = None
    data_points: int = 0


class AlertResponse(BaseModel):
    success: bool
    message: str
    data: Optional[dict] = None


class MetricsResponse(BaseModel):
    success: bool
    message: str
    data: Optional[dict] = None


class ThresholdUpdate(BaseModel):
    name: str = Field(..., description="阈值配置名称")
    value: float = Field(..., description="阈值新值")


class ThresholdInfo(BaseModel):
    name: str
    current_value: float
    default_value: float
    type: str = Field(default="system", description="system或override")


class BatchCollectTarget(BaseModel):
    hostname: str = Field(..., description="目标主机名")
    url: Optional[str] = Field(default=None, description="目标API地址, 未提供则使用模板")


class BatchCollectRequest(BaseModel):
    targets: List[BatchCollectTarget] = Field(..., description="目标服务器列表")
    include_local: bool = Field(default=True, description="是否包含本地采集")


class RankingItem(BaseModel):
    hostname: Optional[str] = None
    device: Optional[str] = None
    interface: Optional[str] = None
    value: float
    metric: str


class RankingResult(BaseModel):
    cpu_ranking: List[Dict[str, Any]] = Field(default_factory=list)
    memory_ranking: List[Dict[str, Any]] = Field(default_factory=list)
    disk_usage_ranking: List[Dict[str, Any]] = Field(default_factory=list)
    disk_io_ranking: List[Dict[str, Any]] = Field(default_factory=list)
    network_ranking: List[Dict[str, Any]] = Field(default_factory=list)
    time_window_minutes: int = Field(default=5)
    top_n: int = Field(default=20)
    generated_at: str = ""


class OverloadedHost(BaseModel):
    hostname: str
    resource: str
    usage: float
    threshold: float


class DenoiseConfigUpdate(BaseModel):
    enabled: bool = Field(default=True, description="是否启用降噪")
    method: str = Field(default="ema", description="降噪方法: ema/kalman/median/savgol")
    ema_alpha: Optional[float] = None
    sample_count: Optional[int] = None
