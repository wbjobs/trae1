import os
from dataclasses import dataclass, field
from typing import Optional, Tuple, List, Dict, Any
from datetime import datetime


@dataclass
class MilvusConfig:
    host: str = os.getenv("MILVUS_HOST", "localhost")
    port: int = int(os.getenv("MILVUS_PORT", "19530"))
    collection_name: str = os.getenv("MILVUS_COLLECTION", "video_fingerprints")
    fingerprint_dim: int = 128
    feature_dim: int = 256
    audio_dim: int = 128
    fingerprint_index_type: str = "BIN_FLAT"
    feature_index_type: str = "IVF_FLAT"
    fingerprint_metric: str = "HAMMING"
    feature_metric: str = "COSINE"
    audio_metric: str = "COSINE"
    nlist: int = 1024
    nprobe: int = 32


@dataclass
class VideoConfig:
    frame_interval: float = 1.0
    target_width: int = 64
    target_height: int = 64
    feature_width: int = 224
    feature_height: int = 224
    max_query_duration: int = 30
    fingerprint_bytes: int = 16


@dataclass
class DeepFeatureConfig:
    model_path: str = os.getenv("FEATURE_MODEL_PATH", "models/mobilenetv3.onnx")
    feature_dim: int = 256
    input_mean: Tuple[float, float, float] = (0.485, 0.456, 0.406)
    input_std: Tuple[float, float, float] = (0.229, 0.224, 0.225)
    use_cuda: bool = False
    intra_op_num_threads: int = 4


@dataclass
class AudioConfig:
    enabled: bool = True
    sample_rate: int = 16000
    n_mfcc: int = 13
    hop_length: int = 512
    audio_interval: float = 1.0
    fingerprint_dim: int = 128


@dataclass
class FusionConfig:
    fingerprint_weight: float = float(os.getenv("FUSION_FP_WEIGHT", "0.2"))
    feature_weight: float = float(os.getenv("FUSION_FEAT_WEIGHT", "0.6"))
    audio_weight: float = float(os.getenv("FUSION_AUDIO_WEIGHT", "0.2"))
    enable_audio: bool = True
    normalize_weights: bool = True

    def __post_init__(self):
        if self.normalize_weights:
            total = self.fingerprint_weight + self.feature_weight + self.audio_weight
            if total > 0:
                self.fingerprint_weight /= total
                self.feature_weight /= total
                self.audio_weight /= total


@dataclass
class MatchingConfig:
    sliding_window_step: int = 1
    similarity_threshold: float = 0.7
    max_results: int = 10
    time_tolerance: float = 2.0
    top_k_candidates: int = 50


@dataclass
class ExportConfig:
    export_dir: str = "exports"
    format: str = "numpy"
    include_frames: bool = False


@dataclass
class ServiceConfig:
    host: str = os.getenv("SERVICE_HOST", "0.0.0.0")
    port: int = int(os.getenv("SERVICE_PORT", "8000"))
    workers: int = int(os.getenv("SERVICE_WORKERS", "4"))
    timeout: int = 300


milvus_config = MilvusConfig()
video_config = VideoConfig()
deep_feature_config = DeepFeatureConfig()
audio_config = AudioConfig()
fusion_config = FusionConfig()
matching_config = MatchingConfig()
export_config = ExportConfig()


@dataclass
class MonitorConfig:
    enabled: bool = os.getenv("MONITOR_ENABLED", "false").lower() == "true"
    check_interval: int = int(os.getenv("MONITOR_INTERVAL", "300"))
    max_concurrent_tasks: int = int(os.getenv("MONITOR_MAX_TASKS", "5"))
    evidence_dir: str = "evidence"
    report_dir: str = "reports"
    auto_start: bool = False
    similarity_threshold: float = float(os.getenv("MONITOR_THRESHOLD", "0.75"))


@dataclass
class RSSFeedConfig:
    name: str
    url: str
    platform: str = "general"
    enabled: bool = True
    check_interval: int = 1800
    max_entries_per_check: int = 20
    last_checked: Optional[datetime] = None


@dataclass
class LiveStreamConfig:
    name: str
    url: str
    station: str = ""
    enabled: bool = True
    check_interval: int = 3600
    scan_duration: int = 120


@dataclass
class WebhookConfig:
    url: str = os.getenv("WEBHOOK_URL", "")
    secret: str = os.getenv("WEBHOOK_SECRET", "")
    enabled: bool = os.getenv("WEBHOOK_ENABLED", "false").lower() == "true"
    retry_count: int = 3
    timeout: int = 30


@dataclass
class BlockAPIConfig:
    enabled: bool = os.getenv("BLOCK_API_ENABLED", "false").lower() == "true"
    api_key: str = os.getenv("BLOCK_API_KEY", "")
    api_endpoint: str = os.getenv("BLOCK_API_ENDPOINT", "")
    platform: str = os.getenv("BLOCK_API_PLATFORM", "bilibili")
    auto_block: bool = os.getenv("AUTO_BLOCK", "false").lower() == "true"
    min_confidence: float = 0.9


@dataclass
class InfringementRecord:
    id: str = ""
    video_id: str = ""
    video_title: str = ""
    video_url: str = ""
    platform: str = ""
    infringer: str = ""
    similarity: float = 0.0
    original_video_id: str = ""
    original_video_title: str = ""
    original_copyright_holder: str = ""
    start_timestamp: float = 0.0
    end_timestamp: float = 0.0
    evidence_screenshots: List[str] = field(default_factory=list)
    report_path: str = ""
    detected_at: datetime = field(default_factory=datetime.now)
    status: str = "detected"
    case_id: str = ""
    notes: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return {
            "id": self.id,
            "video_id": self.video_id,
            "video_title": self.video_title,
            "video_url": self.video_url,
            "platform": self.platform,
            "infringer": self.infringer,
            "similarity": self.similarity,
            "original_video_id": self.original_video_id,
            "original_video_title": self.original_video_title,
            "original_copyright_holder": self.original_copyright_holder,
            "start_timestamp": self.start_timestamp,
            "end_timestamp": self.end_timestamp,
            "evidence_screenshots": self.evidence_screenshots,
            "report_path": self.report_path,
            "detected_at": self.detected_at.isoformat(),
            "status": self.status,
            "case_id": self.case_id,
            "notes": self.notes
        }


@dataclass
class MonitorTask:
    id: str
    name: str
    type: str
    config: Dict[str, Any]
    cron_expression: str = ""
    enabled: bool = True
    last_run: Optional[datetime] = None
    next_run: Optional[datetime] = None
    status: str = "idle"

    def to_dict(self) -> Dict[str, Any]:
        return {
            "id": self.id,
            "name": self.name,
            "type": self.type,
            "config": self.config,
            "cron_expression": self.cron_expression,
            "enabled": self.enabled,
            "last_run": self.last_run.isoformat() if self.last_run else None,
            "next_run": self.next_run.isoformat() if self.next_run else None,
            "status": self.status
        }


monitor_config = MonitorConfig()
webhook_config = WebhookConfig()
block_api_config = BlockAPIConfig()
