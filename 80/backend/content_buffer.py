"""内容缓冲模块

为每条 RTP 流维护一个循环缓冲，保存最近 N 秒的解码后数据（音频 PCM / 视频 YUV/RGB），
当检测到内容异常时，可以截取异常发生前后各 1 秒的数据用于人工复核。
"""

from __future__ import annotations

import logging
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Deque, List, Optional, Tuple

import numpy as np

logger = logging.getLogger(__name__)


@dataclass
class AudioFrame:
    """音频帧。"""

    timestamp: float
    pcm: np.ndarray  # int16, shape: (num_samples,)
    sample_rate: int
    num_channels: int


@dataclass
class VideoFrame:
    """视频帧。"""

    timestamp: float
    data: np.ndarray  # uint8, shape: (height, width, 3) RGB
    width: int
    height: int
    pts: int  # RTP timestamp


@dataclass
class ContentAnalysisResult:
    """AI 内容分析结果。"""

    timestamp: float
    anomaly_score: float  # 0-100
    is_anomaly: bool
    audio_score: float = 0.0
    video_score: float = 0.0
    audio_anomalies: List[str] = field(default_factory=list)
    video_anomalies: List[str] = field(default_factory=list)
    details: dict = field(default_factory=dict)
    # 根因分析
    root_cause: str = ""
    confidence: float = 0.0
    # 关联 RTP 指标
    rtp_loss_rate: float = 0.0
    rtp_jitter: float = 0.0
    rtp_mos: float = 0.0


class ContentBuffer:
    """单条流的内容循环缓冲。"""

    DEFAULT_BUFFER_SECONDS = 3.0  # 保存 3 秒（异常时取前后各 1 秒，共 2 秒）
    DEFAULT_AUDIO_SAMPLE_RATE = 16000
    DEFAULT_VIDEO_FPS = 30

    def __init__(self, ssrc: int, media_kind: str,
                 buffer_seconds: float = DEFAULT_BUFFER_SECONDS) -> None:
        self._ssrc = ssrc
        self._media_kind = media_kind
        self._buffer_seconds = buffer_seconds

        self._audio_buffer: Deque[AudioFrame] = deque()
        self._video_buffer: Deque[VideoFrame] = deque()

        # 解码配置
        self._sample_rate = self.DEFAULT_AUDIO_SAMPLE_RATE
        self._fps = self.DEFAULT_VIDEO_FPS

        # 统计
        self._audio_frames = 0
        self._video_frames = 0

    # ----- 写入 -----

    def push_audio(self, pcm: np.ndarray, sample_rate: int,
                   num_channels: int = 1) -> None:
        self._sample_rate = sample_rate
        frame = AudioFrame(
            timestamp=time.time(),
            pcm=pcm.astype(np.int16),
            sample_rate=sample_rate,
            num_channels=num_channels,
        )
        self._audio_buffer.append(frame)
        self._audio_frames += 1
        self._trim_audio()

    def push_video(self, data: np.ndarray, width: int, height: int,
                   pts: int = 0) -> None:
        frame = VideoFrame(
            timestamp=time.time(),
            data=data.astype(np.uint8),
            width=width,
            height=height,
            pts=pts,
        )
        self._video_buffer.append(frame)
        self._video_frames += 1
        self._trim_video()

    # ----- 读取 -----

    def get_audio_window(self, center_time: Optional[float] = None,
                         before: float = 1.0, after: float = 1.0) -> Optional[np.ndarray]:
        """获取异常发生前后的音频拼接。"""
        if not self._audio_buffer:
            return None

        center = center_time if center_time else time.time()
        start_time = center - before
        end_time = center + after

        selected: List[np.ndarray] = []
        for frame in self._audio_buffer:
            if start_time <= frame.timestamp <= end_time:
                selected.append(frame.pcm)

        if not selected:
            return None
        return np.concatenate(selected)

    def get_video_window(self, center_time: Optional[float] = None,
                         before: float = 1.0, after: float = 1.0) -> List[VideoFrame]:
        """获取异常发生前后的视频帧列表。"""
        if not self._video_buffer:
            return []

        center = center_time if center_time else time.time()
        start_time = center - before
        end_time = center + after

        return [f for f in self._video_buffer if start_time <= f.timestamp <= end_time]

    def get_latest_audio(self, duration_sec: float = 5.0) -> Optional[np.ndarray]:
        """获取最近 duration_sec 秒的音频用于分析。"""
        if not self._audio_buffer:
            return None

        latest = list(self._audio_buffer)[-int(self._sample_rate * duration_sec / 960):]  # ~20ms per frame
        if not latest:
            return None
        return np.concatenate([f.pcm for f in latest])

    def get_latest_video_frames(self, count: int = 5) -> List[VideoFrame]:
        """获取最近 count 帧视频用于分析。"""
        if not self._video_buffer:
            return []
        return list(self._video_buffer)[-count:]

    # ----- 修剪 -----

    def _trim_audio(self) -> None:
        if not self._audio_buffer:
            return
        cutoff = time.time() - self._buffer_seconds
        while self._audio_buffer and self._audio_buffer[0].timestamp < cutoff:
            self._audio_buffer.popleft()

    def _trim_video(self) -> None:
        if not self._video_buffer:
            return
        cutoff = time.time() - self._buffer_seconds
        while self._video_buffer and self._video_buffer[0].timestamp < cutoff:
            self._video_buffer.popleft()

    # ----- 属性 -----

    @property
    def has_audio(self) -> bool:
        return len(self._audio_buffer) > 0

    @property
    def has_video(self) -> bool:
        return len(self._video_buffer) > 0

    @property
    def sample_rate(self) -> int:
        return self._sample_rate

    @property
    def frame_count(self) -> Tuple[int, int]:
        return self._audio_frames, self._video_frames
