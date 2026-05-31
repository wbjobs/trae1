import os
import uuid
import tempfile
import time
import json
import numpy as np
from typing import List, Optional, Dict, Any
from contextlib import asynccontextmanager
from datetime import datetime

from fastapi import FastAPI, File, UploadFile, HTTPException, BackgroundTasks, Query
from fastapi.responses import JSONResponse, Response
from pydantic import BaseModel, Field
from loguru import logger

from config import (
    service_config, video_config, fusion_config, export_config,
    monitor_config, webhook_config, block_api_config
)
from video_processor import VideoProcessor, FrameExtractor
from fingerprint_generator import FingerprintGenerator, FingerprintSequence, MultimodalFingerprint
from milvus_store import MilvusStore
from matcher import FingerprintMatcher
from deep_feature_extractor import DeepFeatureExtractor
from audio_fingerprint import AudioFingerprintExtractor
from rss_monitor import RSSMonitor, RSSVideoEntry
from live_monitor import LiveStreamMonitor
from auto_rights import AutoRightsManager
from scheduler import TaskScheduler
from dashboard import DashboardStats


class ExtractRequest(BaseModel):
    video_url: str = Field(..., description="视频流地址(RTMP/HLS)或本地文件路径")
    video_id: Optional[str] = Field(None, description="视频ID，不传则自动生成")
    enable_deep_feature: bool = Field(True, description="是否启用深度特征提取")
    enable_audio: bool = Field(True, description="是否启用音频指纹提取")
    export_features: bool = Field(False, description="是否导出特征向量")


class ExtractResponse(BaseModel):
    video_id: str
    status: str
    frame_count: int
    duration_seconds: float
    processing_time: float
    has_deep_feature: bool
    has_audio: bool
    export_path: Optional[str] = None


class QueryRequest(BaseModel):
    video_url: Optional[str] = Field(None, description="查询视频片段URL")
    max_duration: int = Field(video_config.max_query_duration, le=video_config.max_query_duration,
                              description="查询视频最大时长(秒)")
    top_k: int = Field(10, ge=1, le=100, description="返回Top-K结果")
    use_multimodal: bool = Field(True, description="是否使用多模态匹配")
    enable_deep_feature: bool = Field(True, description="是否使用深度特征")
    enable_audio: bool = Field(True, description="是否使用音频指纹")


class FusionConfigRequest(BaseModel):
    fingerprint_weight: float = Field(0.2, ge=0.0, le=1.0, description="pHash指纹权重")
    feature_weight: float = Field(0.6, ge=0.0, le=1.0, description="深度特征权重")
    audio_weight: float = Field(0.2, ge=0.0, le=1.0, description="音频指纹权重")


class FusionConfigResponse(BaseModel):
    fingerprint_weight: float
    feature_weight: float
    audio_weight: float
    normalized: bool


class MatchResult(BaseModel):
    video_id: str
    confidence: float
    start_timestamp: float
    end_timestamp: float
    query_length: int
    matched_frames: List[int]
    modalities: Optional[List[str]] = None
    raw_score: Optional[float] = None


class QueryResponse(BaseModel):
    query_id: str
    status: str
    processing_time: float
    results: List[MatchResult]
    result_count: int
    search_mode: str
    top_k: int


class FeatureExportResponse(BaseModel):
    video_id: str
    export_path: str
    frame_count: int
    format: str
    includes: List[str]


class DeleteRequest(BaseModel):
    video_id: str


class StatusResponse(BaseModel):
    status: str
    total_fingerprints: int
    milvus_connected: bool
    deep_feature_enabled: bool
    audio_enabled: bool
    fusion_weights: Dict[str, float]
    monitor_enabled: bool
    total_infringements: int
    scheduled_jobs: int


class RSSFeedRequest(BaseModel):
    name: str = Field(..., description="RSS源名称")
    url: str = Field(..., description="RSS源URL")
    platform: str = Field("general", description="平台类型")
    enabled: bool = Field(True, description="是否启用")
    check_interval: int = Field(1800, description="检查间隔(秒)")


class LiveStreamRequest(BaseModel):
    name: str = Field(..., description="直播流名称")
    url: str = Field(..., description="直播流URL")
    station: str = Field("", description="电视台名称")
    enabled: bool = Field(True, description="是否启用")
    check_interval: int = Field(3600, description="检查间隔(秒)")
    scan_duration: int = Field(120, description="每次扫描时长(秒)")


class CronJobRequest(BaseModel):
    job_id: str = Field(..., description="任务ID")
    name: str = Field(..., description="任务名称")
    cron_expression: str = Field(..., description="Cron表达式")
    task_type: str = Field(..., description="任务类型: rss_scan|livestream_scan")
    target: Optional[str] = Field(None, description="目标名称")


class InfringementRecordRequest(BaseModel):
    status: Optional[str] = Field(None, description="状态过滤")
    platform: Optional[str] = Field(None, description="平台过滤")
    limit: int = Field(100, description="返回数量限制")


class InfringementUpdateRequest(BaseModel):
    status: str = Field(..., description="新状态")
    notes: Optional[str] = Field(None, description="备注")


class DashboardStatsRequest(BaseModel):
    start_date: Optional[str] = Field(None, description="开始日期")
    end_date: Optional[str] = Field(None, description="结束日期")
    group_by: str = Field("day", description="分组方式: hour|day|week|month")


class ServiceManager:
    def __init__(self):
        self.milvus_store: Optional[MilvusStore] = None
        self.fp_generator: Optional[FingerprintGenerator] = None
        self.frame_extractor: Optional[FrameExtractor] = None
        self.video_processor: Optional[VideoProcessor] = None
        self.deep_feature_extractor: Optional[DeepFeatureExtractor] = None
        self.audio_extractor: Optional[AudioFingerprintExtractor] = None
        self.matcher: Optional[FingerprintMatcher] = None
        self.initialized = False
        self.deep_feature_enabled = False
        self.audio_enabled = False
        
        self.rss_monitor: Optional[RSSMonitor] = None
        self.live_monitor: Optional[LiveStreamMonitor] = None
        self.rights_manager: Optional[AutoRightsManager] = None
        self.scheduler: Optional[TaskScheduler] = None
        self.dashboard: Optional[DashboardStats] = None
        self.monitor_enabled = False

    def init(self):
        try:
            logger.info("初始化服务组件...")
            self.milvus_store = MilvusStore()
            self.fp_generator = FingerprintGenerator()
            self.frame_extractor = FrameExtractor()
            self.video_processor = VideoProcessor()
            
            try:
                self.deep_feature_extractor = DeepFeatureExtractor()
                self.deep_feature_enabled = True
                logger.info("深度特征提取器初始化成功")
            except Exception as e:
                logger.warning(f"深度特征提取器初始化失败 (需要ONNX模型): {e}")
                self.deep_feature_enabled = False
            
            try:
                self.audio_extractor = AudioFingerprintExtractor()
                self.audio_enabled = True
                logger.info("音频指纹提取器初始化成功")
            except Exception as e:
                logger.warning(f"音频指纹提取器初始化失败: {e}")
                self.audio_enabled = False
            
            self.matcher = FingerprintMatcher(
                self.milvus_store,
                self.fp_generator,
                self.deep_feature_extractor,
                self.audio_extractor
            )
            
            if monitor_config.enabled:
                self._init_monitor_components()
                self.monitor_enabled = True
            
            self.initialized = True
            logger.info("服务组件初始化完成")
        except Exception as e:
            logger.error(f"服务初始化失败: {e}")
            self.initialized = False

    def _init_monitor_components(self):
        logger.info("初始化监控组件...")
        self.rss_monitor = RSSMonitor()
        self.live_monitor = LiveStreamMonitor(
            self.fp_generator,
            self.matcher,
            self.deep_feature_extractor,
            self.audio_extractor
        )
        self.rights_manager = AutoRightsManager()
        self.scheduler = TaskScheduler()
        self.dashboard = DashboardStats()
        
        os.makedirs(monitor_config.evidence_dir, exist_ok=True)
        os.makedirs(monitor_config.report_dir, exist_ok=True)
        
        self.scheduler.start()
        
        def on_live_infringement(data):
            logger.info(f"直播流发现侵权: {data}")
        
        self.live_monitor.add_infringement_callback(on_live_infringement)
        
        logger.info("监控组件初始化完成")

    def close(self):
        if self.milvus_store:
            self.milvus_store.close()
        if self.deep_feature_extractor:
            self.deep_feature_extractor.close()
        if self.live_monitor:
            self.live_monitor.stop_all_monitoring()
        if self.scheduler:
            self.scheduler.stop()
        logger.info("服务已关闭")


service_manager = ServiceManager()


@asynccontextmanager
async def lifespan(app: FastAPI):
    service_manager.init()
    yield
    service_manager.close()


app = FastAPI(
    title="多模态视频指纹提取与匹配服务",
    description="基于pHash+MobileNetV3深度特征+音频指纹的多模态视频指纹提取与匹配服务，支持二次编码鲁棒性匹配",
    version="2.0.0",
    lifespan=lifespan
)


def check_initialization():
    if not service_manager.initialized:
        raise HTTPException(status_code=503, detail="服务未初始化，请检查Milvus连接")


def export_features(video_id: str, sequence: FingerprintSequence, format: str = "numpy") -> str:
    os.makedirs(export_config.export_dir, exist_ok=True)
    
    export_data = sequence.export()
    
    timestamp = int(time.time())
    filename = f"{video_id}_features_{timestamp}"
    filepath = os.path.join(export_config.export_dir, filename)
    
    if format == "numpy":
        np.savez(
            filepath + ".npz",
            timestamps=np.array(export_data["timestamps"]),
            fingerprints=np.array([bytes.fromhex(fp) for fp in export_data["fingerprints"]]),
            features=np.array([f for f in export_data["features"] if f is not None]),
            audios=np.array([a for a in export_data["audios"] if a is not None]),
            frame_count=export_data["frame_count"],
            video_id=export_data["video_id"]
        )
        return filepath + ".npz"
    elif format == "json":
        with open(filepath + ".json", "w", encoding="utf-8") as f:
            json.dump(export_data, f, ensure_ascii=False, indent=2)
        return filepath + ".json"
    else:
        raise ValueError(f"不支持的导出格式: {format}")


def process_video_extraction(video_url: str, video_id: str,
                            enable_deep_feature: bool = True,
                            enable_audio: bool = True,
                            export_features_flag: bool = False) -> Dict:
    start_time = time.time()
    
    fp_sequence = FingerprintSequence(video_id)
    
    use_deep = enable_deep_feature and service_manager.deep_feature_enabled
    use_audio = enable_audio and service_manager.audio_enabled
    
    audio_fps = []
    if use_audio:
        try:
            audio_fps = service_manager.audio_extractor.extract(video_url)
            logger.info(f"提取音频指纹 {len(audio_fps)} 个")
        except Exception as e:
            logger.warning(f"音频提取失败: {e}")
            use_audio = False
    
    frame_idx = 0
    for frame, timestamp in service_manager.frame_extractor.extract_frames(video_url):
        fingerprint = service_manager.fp_generator.generate(frame)
        
        feature = None
        if use_deep:
            try:
                feature = service_manager.deep_feature_extractor.extract(frame)
            except Exception as e:
                logger.warning(f"深度特征提取失败: {e}")
                feature = None
        
        audio = None
        if use_audio and audio_fps:
            audio = service_manager.audio_extractor.get_audio_fingerprint_at(audio_fps, timestamp)
        
        fp_sequence.add(fingerprint, timestamp, feature, audio)
        frame_idx += 1
    
    multimodal_fps = fp_sequence.get_multimodal_sequence()
    
    if multimodal_fps:
        service_manager.milvus_store.insert_multimodal(video_id, multimodal_fps)
    
    processing_time = time.time() - start_time
    timestamps = fp_sequence.timestamps
    duration = timestamps[-1] if timestamps else 0
    
    export_path = None
    if export_features_flag:
        try:
            export_path = export_features(video_id, fp_sequence)
            logger.info(f"特征已导出到: {export_path}")
        except Exception as e:
            logger.error(f"特征导出失败: {e}")
    
    return {
        "video_id": video_id,
        "frame_count": len(multimodal_fps),
        "duration_seconds": duration,
        "processing_time": processing_time,
        "has_deep_feature": use_deep,
        "has_audio": use_audio,
        "export_path": export_path
    }


def process_video_query(video_url: str, max_duration: int,
                        top_k: int = 10,
                        use_multimodal: bool = True,
                        enable_deep_feature: bool = True,
                        enable_audio: bool = True) -> Dict:
    start_time = time.time()
    
    max_frames = max_duration
    use_deep = enable_deep_feature and service_manager.deep_feature_enabled
    use_audio = enable_audio and service_manager.audio_enabled
    use_mm = use_multimodal and (use_deep or use_audio)
    
    query_multimodal = []
    query_fingerprints = []
    query_timestamps = []
    
    audio_fps = []
    if use_audio:
        try:
            audio_fps = service_manager.audio_extractor.extract(video_url)
        except Exception as e:
            logger.warning(f"查询音频提取失败: {e}")
            use_audio = False
    
    for frame, timestamp in service_manager.frame_extractor.extract_frames(
        video_url, max_frames=max_frames
    ):
        fingerprint = service_manager.fp_generator.generate(frame)
        query_fingerprints.append(fingerprint)
        query_timestamps.append(timestamp)
        
        feature = None
        if use_deep:
            try:
                feature = service_manager.deep_feature_extractor.extract(frame)
            except Exception as e:
                feature = None
        
        audio = None
        if use_audio and audio_fps:
            audio = service_manager.audio_extractor.get_audio_fingerprint_at(audio_fps, timestamp)
        
        query_multimodal.append(MultimodalFingerprint(
            fingerprint=fingerprint,
            feature=feature,
            audio=audio,
            timestamp=timestamp,
            frame_index=len(query_fingerprints) - 1
        ))
    
    if not query_fingerprints:
        return {
            "results": [],
            "processing_time": time.time() - start_time,
            "status": "no_frames_extracted",
            "search_mode": "none"
        }
    
    if use_mm:
        logger.info("使用多模态匹配...")
        results = service_manager.matcher.fast_match_multimodal(query_multimodal, top_k=top_k)
        search_mode = "multimodal"
    else:
        logger.info("使用传统pHash匹配...")
        results = service_manager.matcher.fast_match(query_fingerprints)
        search_mode = "phash_only"
    
    processing_time = time.time() - start_time
    
    return {
        "results": results,
        "processing_time": processing_time,
        "status": "success",
        "search_mode": search_mode
    }


@app.post("/api/v1/extract", response_model=ExtractResponse)
async def extract_fingerprints(request: ExtractRequest, background_tasks: BackgroundTasks):
    check_initialization()
    
    video_id = request.video_id or str(uuid.uuid4())
    
    try:
        result = process_video_extraction(
            request.video_url, video_id,
            enable_deep_feature=request.enable_deep_feature,
            enable_audio=request.enable_audio,
            export_features_flag=request.export_features
        )
        
        return ExtractResponse(
            video_id=video_id,
            status="completed",
            frame_count=result["frame_count"],
            duration_seconds=result["duration_seconds"],
            processing_time=round(result["processing_time"], 3),
            has_deep_feature=result["has_deep_feature"],
            has_audio=result["has_audio"],
            export_path=result["export_path"]
        )
    except Exception as e:
        logger.error(f"提取指纹失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/api/v1/query", response_model=QueryResponse)
async def query_video(request: QueryRequest, background_tasks: BackgroundTasks):
    check_initialization()
    
    if not request.video_url:
        raise HTTPException(status_code=400, detail="必须提供video_url")
    
    query_id = str(uuid.uuid4())
    
    try:
        result = process_video_query(
            request.video_url,
            max_duration=request.max_duration,
            top_k=request.top_k,
            use_multimodal=request.use_multimodal,
            enable_deep_feature=request.enable_deep_feature,
            enable_audio=request.enable_audio
        )
        
        match_results = []
        for r in result["results"]:
            match_results.append(MatchResult(
                video_id=r["video_id"],
                confidence=r["confidence"],
                start_timestamp=r["start_timestamp"],
                end_timestamp=r["end_timestamp"],
                query_length=r["query_length"],
                matched_frames=list(r["matched_frames"]),
                modalities=r.get("modalities"),
                raw_score=r.get("raw_score")
            ))
        
        return QueryResponse(
            query_id=query_id,
            status=result["status"],
            processing_time=round(result["processing_time"], 3),
            results=match_results,
            result_count=len(match_results),
            search_mode=result.get("search_mode", "unknown"),
            top_k=request.top_k
        )
    except Exception as e:
        logger.error(f"查询失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/api/v1/query/upload", response_model=QueryResponse)
async def query_video_upload(
    file: UploadFile = File(...),
    max_duration: int = video_config.max_query_duration,
    top_k: int = Query(10, ge=1, le=100),
    use_multimodal: bool = Query(True),
    enable_deep_feature: bool = Query(True),
    enable_audio: bool = Query(True)
):
    check_initialization()
    
    query_id = str(uuid.uuid4())
    
    try:
        with tempfile.NamedTemporaryFile(delete=False, suffix=os.path.splitext(file.filename)[1]) as tmp:
            content = await file.read()
            tmp.write(content)
            tmp_path = tmp.name
        
        result = process_video_query(
            tmp_path,
            max_duration=max_duration,
            top_k=top_k,
            use_multimodal=use_multimodal,
            enable_deep_feature=enable_deep_feature,
            enable_audio=enable_audio
        )
        
        os.unlink(tmp_path)
        
        match_results = []
        for r in result["results"]:
            match_results.append(MatchResult(
                video_id=r["video_id"],
                confidence=r["confidence"],
                start_timestamp=r["start_timestamp"],
                end_timestamp=r["end_timestamp"],
                query_length=r["query_length"],
                matched_frames=list(r["matched_frames"]),
                modalities=r.get("modalities"),
                raw_score=r.get("raw_score")
            ))
        
        return QueryResponse(
            query_id=query_id,
            status=result["status"],
            processing_time=round(result["processing_time"], 3),
            results=match_results,
            result_count=len(match_results),
            search_mode=result.get("search_mode", "unknown"),
            top_k=top_k
        )
    except Exception as e:
        logger.error(f"上传查询失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/api/v1/export/{video_id}", response_model=FeatureExportResponse)
async def export_video_features(
    video_id: str,
    format: str = Query("numpy", enum=["numpy", "json"]),
    include_frames: bool = Query(False)
):
    check_initialization()
    
    try:
        fps_list = service_manager.milvus_store.get_video_fingerprints(video_id)
        if not fps_list:
            raise HTTPException(status_code=404, detail=f"未找到视频 {video_id} 的指纹")
        
        sequence = FingerprintSequence(video_id)
        for fp_data in fps_list:
            sequence.add(
                fingerprint=fp_data["fingerprint"],
                timestamp=fp_data["timestamp"],
                feature=fp_data["feature"],
                audio=fp_data["audio"]
            )
        
        export_path = export_features(video_id, sequence, format=format)
        
        includes = ["fingerprints", "timestamps"]
        if any(fp_data["feature"] is not None for fp_data in fps_list):
            includes.append("features")
        if any(fp_data["audio"] is not None for fp_data in fps_list):
            includes.append("audios")
        if include_frames:
            includes.append("frames")
        
        return FeatureExportResponse(
            video_id=video_id,
            export_path=export_path,
            frame_count=len(fps_list),
            format=format,
            includes=includes
        )
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"导出特征失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/api/v1/export/download/{video_id}")
async def download_exported_features(video_id: str, format: str = Query("numpy", enum=["numpy", "json"])):
    check_initialization()
    
    try:
        fps_list = service_manager.milvus_store.get_video_fingerprints(video_id)
        if not fps_list:
            raise HTTPException(status_code=404, detail=f"未找到视频 {video_id} 的指纹")
        
        sequence = FingerprintSequence(video_id)
        for fp_data in fps_list:
            sequence.add(
                fingerprint=fp_data["fingerprint"],
                timestamp=fp_data["timestamp"],
                feature=fp_data["feature"],
                audio=fp_data["audio"]
            )
        
        export_path = export_features(video_id, sequence, format=format)
        
        filename = os.path.basename(export_path)
        
        with open(export_path, "rb") as f:
            content = f.read()
        
        media_type = "application/octet-stream" if format == "numpy" else "application/json"
        
        return Response(
            content=content,
            media_type=media_type,
            headers={"Content-Disposition": f"attachment; filename=\"{filename}\""})
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"下载导出文件失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/api/v1/fusion/config", response_model=FusionConfigResponse)
async def get_fusion_config():
    return FusionConfigResponse(
        fingerprint_weight=fusion_config.fingerprint_weight,
        feature_weight=fusion_config.feature_weight,
        audio_weight=fusion_config.audio_weight,
        normalized=fusion_config.normalize_weights
    )


@app.post("/api/v1/fusion/config", response_model=FusionConfigResponse)
async def set_fusion_config(request: FusionConfigRequest):
    total = request.fingerprint_weight + request.feature_weight + request.audio_weight
    if total <= 0:
        raise HTTPException(status_code=400, detail="权重之和必须大于0")
    
    if fusion_config.normalize_weights:
        fusion_config.fingerprint_weight = request.fingerprint_weight / total
        fusion_config.feature_weight = request.feature_weight / total
        fusion_config.audio_weight = request.audio_weight / total
    else:
        fusion_config.fingerprint_weight = request.fingerprint_weight
        fusion_config.feature_weight = request.feature_weight
        fusion_config.audio_weight = request.audio_weight
    
    if service_manager.fp_generator:
        service_manager.fp_generator.fp_weight = fusion_config.fingerprint_weight
        service_manager.fp_generator.feat_weight = fusion_config.feature_weight
        service_manager.fp_generator.audio_weight = fusion_config.audio_weight
    
    if service_manager.matcher:
        service_manager.matcher.fp_weight = fusion_config.fingerprint_weight
        service_manager.matcher.feat_weight = fusion_config.feature_weight
        service_manager.matcher.audio_weight = fusion_config.audio_weight
    
    logger.info(f"融合权重已更新: fp={fusion_config.fingerprint_weight:.3f}, "
                f"feat={fusion_config.feature_weight:.3f}, "
                f"audio={fusion_config.audio_weight:.3f}")
    
    return FusionConfigResponse(
        fingerprint_weight=fusion_config.fingerprint_weight,
        feature_weight=fusion_config.feature_weight,
        audio_weight=fusion_config.audio_weight,
        normalized=fusion_config.normalize_weights
    )


@app.delete("/api/v1/video/{video_id}")
async def delete_video(video_id: str):
    check_initialization()
    
    try:
        success = service_manager.milvus_store.delete_by_video_id(video_id)
        if success:
            return {"status": "success", "message": f"已删除视频 {video_id} 的指纹"}
        else:
            raise HTTPException(status_code=500, detail="删除失败")
    except Exception as e:
        logger.error(f"删除视频失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/api/v1/status", response_model=StatusResponse)
async def get_status():
    try:
        total = service_manager.milvus_store.count() if service_manager.milvus_store else 0
        total_infringements = 0
        scheduled_jobs = 0
        
        if service_manager.rights_manager:
            total_infringements = len(service_manager.rights_manager.infringement_records)
        if service_manager.scheduler:
            scheduled_jobs = len(service_manager.scheduler.jobs)
        
        return StatusResponse(
            status="running" if service_manager.initialized else "error",
            total_fingerprints=total,
            milvus_connected=service_manager.initialized,
            deep_feature_enabled=service_manager.deep_feature_enabled,
            audio_enabled=service_manager.audio_enabled,
            fusion_weights={
                "fingerprint": fusion_config.fingerprint_weight,
                "feature": fusion_config.feature_weight,
                "audio": fusion_config.audio_weight
            },
            monitor_enabled=service_manager.monitor_enabled,
            total_infringements=total_infringements,
            scheduled_jobs=scheduled_jobs
        )
    except Exception as e:
        logger.error(f"获取状态失败: {e}")
        return StatusResponse(
            status="error",
            total_fingerprints=0,
            milvus_connected=False,
            deep_feature_enabled=False,
            audio_enabled=False,
            fusion_weights={
                "fingerprint": 0.2,
                "feature": 0.6,
                "audio": 0.2
            },
            monitor_enabled=False,
            total_infringements=0,
            scheduled_jobs=0
        )


@app.get("/api/v1/video/{video_id}/count")
async def get_video_fingerprint_count(video_id: str):
    check_initialization()
    
    try:
        fps_list = service_manager.milvus_store.get_video_fingerprints(video_id)
        return {
            "video_id": video_id,
            "fingerprint_count": len(fps_list)
        }
    except Exception as e:
        logger.error(f"获取视频指纹数量失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/health")
async def health_check():
    return {
        "status": "healthy",
        "service": "video-fingerprint-service",
        "initialized": service_manager.initialized
    }


def check_monitor_enabled():
    if not service_manager.monitor_enabled or not service_manager.rss_monitor:
        raise HTTPException(status_code=503, detail="监控功能未启用，请设置MONITOR_ENABLED=true")


@app.get("/api/v1/monitor/rss/feeds")
async def get_rss_feeds():
    check_monitor_enabled()
    return {"feeds": service_manager.rss_monitor.get_feeds()}


@app.post("/api/v1/monitor/rss/feeds")
async def add_rss_feed(request: RSSFeedRequest):
    check_monitor_enabled()
    from config import RSSFeedConfig
    
    config = RSSFeedConfig(
        name=request.name,
        url=request.url,
        platform=request.platform,
        enabled=request.enabled,
        check_interval=request.check_interval
    )
    service_manager.rss_monitor.add_feed(config)
    return {"status": "success", "message": f"已添加RSS源: {request.name}"}


@app.delete("/api/v1/monitor/rss/feeds/{name}")
async def remove_rss_feed(name: str):
    check_monitor_enabled()
    success = service_manager.rss_monitor.remove_feed(name)
    if success:
        return {"status": "success", "message": f"已删除RSS源: {name}"}
    raise HTTPException(status_code=404, detail=f"未找到RSS源: {name}")


@app.post("/api/v1/monitor/rss/scan")
async def scan_rss_feeds(background_tasks: BackgroundTasks):
    check_monitor_enabled()
    
    async def scan_and_process():
        results = service_manager.rss_monitor.check_all_feeds()
        
        for feed_name, entries in results.items():
            for entry in entries[:5]:
                try:
                    query_result = process_video_query(
                        entry.url,
                        max_duration=30,
                        top_k=10,
                        use_multimodal=True,
                        enable_deep_feature=True,
                        enable_audio=True
                    )
                    
                    for match in query_result.get("results", []):
                        if match["confidence"] >= monitor_config.similarity_threshold:
                            record = await service_manager.rights_manager.process_infringement(
                                match,
                                {
                                    "video_id": entry.video_id,
                                    "title": entry.title,
                                    "url": entry.url,
                                    "author": entry.author
                                },
                                {
                                    "video_id": match["video_id"],
                                    "title": match["video_id"],
                                    "copyright_holder": "未知"
                                },
                                feed_name
                            )
                            service_manager.dashboard.add_infringement_record(record)
                            
                except Exception as e:
                    logger.error(f"处理RSS视频失败 {entry.url}: {e}")
    
    background_tasks.add_task(scan_and_process)
    return {"status": "success", "message": "RSS扫描已启动"}


@app.get("/api/v1/monitor/live/streams")
async def get_live_streams():
    check_monitor_enabled()
    return {"streams": service_manager.live_monitor.get_streams()}


@app.post("/api/v1/monitor/live/streams")
async def add_live_stream(request: LiveStreamRequest):
    check_monitor_enabled()
    from config import LiveStreamConfig
    
    config = LiveStreamConfig(
        name=request.name,
        url=request.url,
        station=request.station,
        enabled=request.enabled,
        check_interval=request.check_interval,
        scan_duration=request.scan_duration
    )
    service_manager.live_monitor.add_stream(config)
    return {"status": "success", "message": f"已添加直播流: {request.name}"}


@app.delete("/api/v1/monitor/live/streams/{name}")
async def remove_live_stream(name: str):
    check_monitor_enabled()
    success = service_manager.live_monitor.remove_stream(name)
    if success:
        return {"status": "success", "message": f"已删除直播流: {name}"}
    raise HTTPException(status_code=404, detail=f"未找到直播流: {name}")


@app.post("/api/v1/monitor/live/scan/{name}")
async def scan_live_stream(name: str, background_tasks: BackgroundTasks):
    check_monitor_enabled()
    
    stream_config = None
    for stream in service_manager.live_monitor.streams:
        if stream.name == name:
            stream_config = stream
            break
    
    if not stream_config:
        raise HTTPException(status_code=404, detail=f"未找到直播流: {name}")
    
    background_tasks.add_task(service_manager.live_monitor.scan_stream, stream_config)
    return {"status": "success", "message": f"直播流扫描已启动: {name}"}


@app.post("/api/v1/monitor/live/start/{name}")
async def start_live_monitoring(name: str):
    check_monitor_enabled()
    success = service_manager.live_monitor.start_monitoring_stream(name)
    if success:
        return {"status": "success", "message": f"已启动直播监控: {name}"}
    raise HTTPException(status_code=400, detail=f"启动直播监控失败: {name}")


@app.post("/api/v1/monitor/live/stop/{name}")
async def stop_live_monitoring(name: str):
    check_monitor_enabled()
    success = service_manager.live_monitor.stop_monitoring_stream(name)
    if success:
        return {"status": "success", "message": f"已停止直播监控: {name}"}
    raise HTTPException(status_code=400, detail=f"停止直播监控失败: {name}")


@app.get("/api/v1/monitor/live/results")
async def get_live_scan_results():
    check_monitor_enabled()
    return {"results": service_manager.live_monitor.get_scan_results()}


@app.get("/api/v1/scheduler/jobs")
async def get_scheduler_jobs():
    check_monitor_enabled()
    return {"jobs": service_manager.scheduler.get_jobs()}


@app.post("/api/v1/scheduler/jobs")
async def add_scheduler_job(request: CronJobRequest):
    check_monitor_enabled()
    
    def job_wrapper():
        if request.task_type == "rss_scan":
            results = service_manager.rss_monitor.check_all_feeds()
            logger.info(f"定时任务RSS扫描完成: {len(results)} 个源有更新")
        elif request.task_type == "livestream_scan":
            service_manager.live_monitor.scan_all_streams()
            logger.info("定时任务直播流扫描完成")
    
    success = service_manager.scheduler.add_job(
        request.job_id,
        request.name,
        request.cron_expression,
        job_wrapper
    )
    
    if success:
        return {"status": "success", "message": f"已添加定时任务: {request.name}"}
    raise HTTPException(status_code=400, detail=f"定时任务已存在: {request.job_id}")


@app.delete("/api/v1/scheduler/jobs/{job_id}")
async def remove_scheduler_job(job_id: str):
    check_monitor_enabled()
    success = service_manager.scheduler.remove_job(job_id)
    if success:
        return {"status": "success", "message": f"已删除定时任务: {job_id}"}
    raise HTTPException(status_code=404, detail=f"未找到定时任务: {job_id}")


@app.post("/api/v1/scheduler/jobs/{job_id}/run")
async def run_scheduler_job_now(job_id: str):
    check_monitor_enabled()
    success = service_manager.scheduler.run_job_now(job_id)
    if success:
        return {"status": "success", "message": f"任务已执行: {job_id}"}
    raise HTTPException(status_code=404, detail=f"未找到定时任务: {job_id}")


@app.get("/api/v1/infringements")
async def get_infringement_records(
    status: Optional[str] = None,
    platform: Optional[str] = None,
    limit: int = 100
):
    check_monitor_enabled()
    records = service_manager.rights_manager.get_infringement_records(
        status=status,
        platform=platform,
        limit=limit
    )
    return {
        "total": len(records),
        "records": [r.to_dict() for r in records]
    }


@app.get("/api/v1/infringements/{record_id}")
async def get_infringement_record(record_id: str):
    check_monitor_enabled()
    record = service_manager.rights_manager.get_record_by_id(record_id)
    if record:
        return record.to_dict()
    raise HTTPException(status_code=404, detail=f"未找到侵权记录: {record_id}")


@app.put("/api/v1/infringements/{record_id}")
async def update_infringement_record(record_id: str, request: InfringementUpdateRequest):
    check_monitor_enabled()
    success = service_manager.rights_manager.update_record_status(
        record_id,
        request.status,
        request.notes
    )
    if success:
        return {"status": "success", "message": f"已更新侵权记录状态: {record_id}"}
    raise HTTPException(status_code=404, detail=f"未找到侵权记录: {record_id}")


@app.get("/api/v1/dashboard/overview")
async def get_dashboard_overview():
    check_monitor_enabled()
    return service_manager.dashboard.get_overview_stats()


@app.get("/api/v1/dashboard/infringements/by-platform")
async def get_infringements_by_platform(
    start_date: Optional[str] = None,
    end_date: Optional[str] = None
):
    check_monitor_enabled()
    start = datetime.fromisoformat(start_date) if start_date else None
    end = datetime.fromisoformat(end_date) if end_date else None
    return service_manager.dashboard.get_infringements_by_platform(start, end)


@app.get("/api/v1/dashboard/infringements/by-infringer")
async def get_infringements_by_infringer(
    start_date: Optional[str] = None,
    end_date: Optional[str] = None,
    top_n: int = 10
):
    check_monitor_enabled()
    start = datetime.fromisoformat(start_date) if start_date else None
    end = datetime.fromisoformat(end_date) if end_date else None
    return {"top_infringers": service_manager.dashboard.get_infringements_by_infringer(start, end, top_n)}


@app.get("/api/v1/dashboard/infringements/by-similarity")
async def get_infringements_by_similarity(
    start_date: Optional[str] = None,
    end_date: Optional[str] = None
):
    check_monitor_enabled()
    start = datetime.fromisoformat(start_date) if start_date else None
    end = datetime.fromisoformat(end_date) if end_date else None
    return service_manager.dashboard.get_infringements_by_similarity(start, end)


@app.get("/api/v1/dashboard/infringements/by-time")
async def get_infringements_by_time(
    start_date: Optional[str] = None,
    end_date: Optional[str] = None,
    group_by: str = "day"
):
    check_monitor_enabled()
    start = datetime.fromisoformat(start_date) if start_date else None
    end = datetime.fromisoformat(end_date) if end_date else None
    return service_manager.dashboard.get_infringements_by_time(start, end, group_by)


@app.get("/api/v1/dashboard/top-original-videos")
async def get_top_original_videos(
    start_date: Optional[str] = None,
    end_date: Optional[str] = None,
    top_n: int = 10
):
    check_monitor_enabled()
    start = datetime.fromisoformat(start_date) if start_date else None
    end = datetime.fromisoformat(end_date) if end_date else None
    return {"top_videos": service_manager.dashboard.get_top_original_videos(start, end, top_n)}


@app.get("/api/v1/dashboard/trend")
async def get_trend_data(days: int = 7):
    check_monitor_enabled()
    return service_manager.dashboard.get_trend_data(days=days)


@app.get("/api/v1/dashboard/monitor-performance")
async def get_monitor_performance():
    check_monitor_enabled()
    return {"performance": service_manager.dashboard.get_monitor_performance()}


@app.get("/api/v1/config/webhook")
async def get_webhook_config():
    return {
        "enabled": webhook_config.enabled,
        "url": webhook_config.url if webhook_config.enabled else "",
        "timeout": webhook_config.timeout,
        "retry_count": webhook_config.retry_count
    }


@app.get("/api/v1/config/block-api")
async def get_block_api_config():
    return {
        "enabled": block_api_config.enabled,
        "platform": block_api_config.platform,
        "auto_block": block_api_config.auto_block,
        "min_confidence": block_api_config.min_confidence
    }


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        "main:app",
        host=service_config.host,
        port=service_config.port,
        workers=service_config.workers
    )
