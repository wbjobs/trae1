"""RTP 流注册表与时间窗口管理。

- StreamRegistry：按 SSRC 维护所有活跃流的 StreamAnalyzer
- 定时快照轮询
- 质量事件通知（MOS < 3 时触发回调）
- AI 内容分析集成：每 5 秒分析音频/视频内容
"""

from __future__ import annotations

import asyncio
import logging
import time
from dataclasses import dataclass, field
from typing import Callable, Deque, Dict, List, Optional

import numpy as np

from .ai_analyzer import ContentAIAnalyzer, ContentAnalysisResult
from .capture import CaptureManager
from .content_buffer import ContentBuffer
from .quality import StreamAnalyzer, StreamMetrics
from .rtp_parser import RtpPacket, guess_media_kind

logger = logging.getLogger(__name__)


MosAlertCallback = Callable[[StreamAnalyzer, StreamMetrics], None]
ContentAlertCallback = Callable[[StreamAnalyzer, ContentBuffer, ContentAnalysisResult], None]


@dataclass
class StreamPipeline:
    """单条流的完整处理流水线。"""

    analyzer: StreamAnalyzer
    content_buffer: ContentBuffer
    ai_analyzer: ContentAIAnalyzer
    last_content_alert: float = 0.0
    # 解码缓冲
    _video_packets: Deque[RtpPacket] = field(default_factory=lambda: Deque(maxlen=100))
    _audio_packets: Deque[RtpPacket] = field(default_factory=lambda: Deque(maxlen=500))
    _decoder = None
    _audio_decoder = None


class StreamRegistry:
    """SSRC -> StreamPipeline 的注册表，集成 AI 内容分析。"""

    STREAM_TIMEOUT = 30.0  # 30 秒无数据视为流结束
    CONTENT_ALERT_MIN_INTERVAL = 10.0  # 内容异常最小间隔

    def __init__(self, mos_alert_threshold: float = 3.0,
                 on_mos_alert: Optional[MosAlertCallback] = None,
                 on_content_alert: Optional[ContentAlertCallback] = None,
                 capture_manager: Optional[CaptureManager] = None) -> None:
        self._streams: Dict[int, StreamPipeline] = {}
        self._mos_threshold = mos_alert_threshold
        self._on_mos_alert = on_mos_alert
        self._on_content_alert = on_content_alert
        self._capture = capture_manager
        self._lock = asyncio.Lock()
        self._ai_enabled = True

    async def feed_packet(self, pkt: RtpPacket, source_ip: str, source_port: int) -> None:
        async with self._lock:
            pipeline = self._streams.get(pkt.ssrc)
            if pipeline is None:
                kind, codec = guess_media_kind(pkt.payload_type)
                analyzer = StreamAnalyzer(
                    ssrc=pkt.ssrc,
                    media_kind=kind,
                    codec=codec,
                    source_ip=source_ip,
                    source_port=source_port,
                    payload_type=pkt.payload_type,
                )
                content_buffer = ContentBuffer(
                    ssrc=pkt.ssrc,
                    media_kind=kind,
                )
                ai_analyzer = ContentAIAnalyzer(
                    ssrc=pkt.ssrc,
                    media_kind=kind,
                )
                pipeline = StreamPipeline(
                    analyzer=analyzer,
                    content_buffer=content_buffer,
                    ai_analyzer=ai_analyzer,
                )
                self._streams[pkt.ssrc] = pipeline
                logger.info(
                    "New stream detected: SSRC=0x%08X kind=%s codec=%s from %s:%d",
                    pkt.ssrc, kind, codec, source_ip, source_port,
                )
            else:
                if not pipeline.analyzer.info.source_ip:
                    pipeline.analyzer.info.source_ip = source_ip
                    pipeline.analyzer.info.source_port = source_port

            # RTP 质量分析
            snap = pipeline.analyzer.feed(pkt)

            # 将包送入解码缓冲用于后续内容分析
            if pipeline.analyzer.info.media_kind == "video":
                pipeline._video_packets.append(pkt)
                # 尝试解码并送入 content buffer
                self._try_decode_video(pipeline, pkt)
            elif pipeline.analyzer.info.media_kind == "audio":
                pipeline._audio_packets.append(pkt)
                # 尝试解码音频并送入 content buffer
                self._try_decode_audio(pipeline, pkt)

            # MOS 告警
            if snap is not None and snap.mos < self._mos_threshold and self._on_mos_alert:
                try:
                    self._on_mos_alert(pipeline.analyzer, snap)
                except Exception:
                    logger.exception("MOS alert callback failed")

            # AI 内容分析（每 5 秒自动执行）
            if snap is not None and self._ai_enabled:
                try:
                    # 使用 capture manager 的解码数据（如果有）
                    self._fill_content_buffer_from_capture(pipeline)
                    ai_result = pipeline.ai_analyzer.analyze(
                        pipeline.content_buffer,
                        snap,
                    )
                    if ai_result is not None and ai_result.is_anomaly:
                        self._handle_content_anomaly(pipeline, ai_result)
                except Exception:
                    logger.exception("AI content analysis failed for SSRC=0x%08X", pkt.ssrc)

    def _fill_content_buffer_from_capture(self, pipeline: StreamPipeline) -> None:
        """从 CaptureManager 获取已解码的数据填充到 content buffer。"""
        if self._capture is None:
            return
        ssrc = pipeline.analyzer.info.ssrc
        media_kind = pipeline.analyzer.info.media_kind

        # 视频：获取已解码的 RGB 帧
        if media_kind == "video" and self._capture.has_decoded_video(ssrc):
            frames = self._capture.get_last_rgb_frames(ssrc, count=30)
            for f in frames:
                pipeline.content_buffer.push_video(f.data, f.width, f.height, f.pts)

        # 音频：获取已解码的 PCM
        if media_kind == "audio" and self._capture.has_decoded_audio(ssrc):
            pcm = self._capture.get_last_pcm(ssrc, duration_sec=3.0)
            if pcm is not None and len(pcm) > 0:
                sample_rate = self._capture.get_sample_rate(ssrc)
                pipeline.content_buffer.push_audio(pcm, sample_rate, 1)

    def _try_decode_video(self, pipeline: StreamPipeline, pkt: RtpPacket) -> None:
        """尝试从 RTP 包解码视频帧（轻量级实现，不强制依赖 PyAV）。"""
        # 轻量级实现：如果有 SPS/PPS 且有 IDR 帧，尝试构造一个可解码的片段
        # 为了简化和健壮性，这里只记录 marker 位，实际解码依赖 capture manager
        if pkt.marker and self._capture is not None:
            # 尝试解码关键帧并送入 capture manager
            try:
                img = self._capture._decode_h264_frame(pkt.payload, pipeline.analyzer)
                if img is not None:
                    arr = np.array(img)
                    self._capture.feed_decoded_video(
                        pipeline.analyzer.info.ssrc,
                        arr,
                        arr.shape[1],
                        arr.shape[0],
                    )
            except Exception:
                pass

    def _try_decode_audio(self, pipeline: StreamPipeline, pkt: RtpPacket) -> None:
        """尝试将 G.711 音频解码为 PCM。"""
        if self._capture is None or not pkt.payload:
            return
        try:
            pt = pipeline.analyzer.info.payload_type
            if pt in (0, 8):
                is_alaw = (pt == 8)
                pcm_bytes = CaptureManager._g711_to_pcm(pkt.payload, is_alaw)
                pcm = np.frombuffer(pcm_bytes, dtype=np.int16)
                sample_rate = 8000
                self._capture.feed_decoded_audio(
                    pipeline.analyzer.info.ssrc,
                    pcm,
                    sample_rate,
                    1,
                )
                # 同时填充 content buffer
                pipeline.content_buffer.push_audio(pcm, sample_rate, 1)
            elif pt == 9:  # G.722
                pcm = np.frombuffer(pkt.payload, dtype=np.int16)
                sample_rate = 16000
                self._capture.feed_decoded_audio(
                    pipeline.analyzer.info.ssrc,
                    pcm,
                    sample_rate,
                    1,
                )
                pipeline.content_buffer.push_audio(pcm, sample_rate, 1)
        except Exception:
            pass

    def _handle_content_anomaly(self, pipeline: StreamPipeline,
                                ai_result: ContentAnalysisResult) -> None:
        """处理内容异常：触发回调并保存片段。"""
        now = time.time()
        if now - pipeline.last_content_alert < self.CONTENT_ALERT_MIN_INTERVAL:
            return
        pipeline.last_content_alert = now

        logger.warning(
            "Content anomaly: SSRC=0x%08X score=%.1f "
            "audio=%s video=%s root_cause=%s",
            pipeline.analyzer.info.ssrc,
            ai_result.anomaly_score,
            ",".join(ai_result.audio_anomalies) or "none",
            ",".join(ai_result.video_anomalies) or "none",
            ai_result.root_cause,
        )

        if self._on_content_alert:
            try:
                self._on_content_alert(pipeline.analyzer, pipeline.content_buffer, ai_result)
            except Exception:
                logger.exception("Content alert callback failed")

    # ----- 公开 API -----

    def get_latest_ai_result(self, ssrc: int) -> Optional[ContentAnalysisResult]:
        """获取指定流的最新 AI 分析结果。"""
        pipeline = self._streams.get(ssrc)
        if pipeline is None:
            return None
        return pipeline.ai_analyzer.get_latest_result()

    def get_all_ai_results(self) -> Dict[int, Optional[ContentAnalysisResult]]:
        """获取所有流的最新 AI 分析结果。"""
        return {
            ssrc: pipeline.ai_analyzer.get_latest_result()
            for ssrc, pipeline in self._streams.items()
        }

    def cleanup_stale(self) -> None:
        now = time.time()
        stale = [ssrc for ssrc, p in self._streams.items()
                 if now - p.analyzer.info.last_seen > self.STREAM_TIMEOUT]
        for ssrc in stale:
            logger.info("Stream SSRC=0x%08X timed out, removing", ssrc)
            del self._streams[ssrc]

    def get_all_streams(self) -> List[StreamAnalyzer]:
        return [p.analyzer for p in self._streams.values()]

    def get_pipeline(self, ssrc: int) -> Optional[StreamPipeline]:
        return self._streams.get(ssrc)

    def get_stream(self, ssrc: int) -> Optional[StreamAnalyzer]:
        p = self._streams.get(ssrc)
        return p.analyzer if p else None

    def get_stream_count(self) -> int:
        return len(self._streams)

    def set_ai_enabled(self, enabled: bool) -> None:
        self._ai_enabled = enabled

    @property
    def ai_enabled(self) -> bool:
        return self._ai_enabled

    def clear(self) -> None:
        self._streams.clear()
