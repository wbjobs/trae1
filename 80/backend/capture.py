"""截图与录音模块

当 MOS 低于阈值时：
- 视频流：从 RTP 负载中提取最近的关键帧，保存为 JPEG 截图
- 音频流：将最近几秒的 RTP 音频负载保存为 WAV 文件

当 AI 检测到内容异常时：
- 保存异常片段（前后各 1 秒）为 MP4/WAV
- 同时保存 JSON 元数据（根因分析、AI 分数、RTP 指标）
"""

from __future__ import annotations

import io
import json
import logging
import struct
import time
from pathlib import Path
from typing import Deque, Dict, List, Optional, Tuple

import numpy as np

from .ai_analyzer import ContentAnalysisResult
from .content_buffer import ContentBuffer
from .quality import StreamAnalyzer, StreamMetrics
from .rtp_parser import RtpPacket

logger = logging.getLogger(__name__)


class CaptureManager:
    """管理截图、录音、异常片段的触发与保存。"""

    def __init__(self, screenshot_dir: str = "screenshots",
                 recording_dir: str = "recordings",
                 anomaly_dir: str = "anomalies") -> None:
        self._screenshot_dir = Path(screenshot_dir)
        self._recording_dir = Path(recording_dir)
        self._anomaly_dir = Path(anomaly_dir)
        self._screenshot_dir.mkdir(parents=True, exist_ok=True)
        self._recording_dir.mkdir(parents=True, exist_ok=True)
        self._anomaly_dir.mkdir(parents=True, exist_ok=True)

        # 视频关键帧缓冲：按 SSRC 缓存最近的完整帧（H.264 I-frame）
        self._keyframe_buffer: Dict[int, bytes] = {}
        # 音频缓冲：按 SSRC 缓存最近 5 秒的 PCM/G711 数据
        self._audio_buffer: Dict[int, Deque[bytes]] = {}
        # 最近的 RGB 帧（用于 AI 分析和异常截取）
        self._last_rgb_frames: Dict[int, List[Tuple[float, np.ndarray]]] = {}
        # 最近的 PCM 音频（用于 AI 分析和异常截取）
        self._last_pcm: Dict[int, List[Tuple[float, np.ndarray, int]]] = {}

        self._last_capture: Dict[int, float] = {}
        self._last_anomaly_capture: Dict[int, float] = {}
        self._min_interval = 5.0  # 同一流至少间隔 5 秒才再次触发
        self._anomaly_min_interval = 10.0  # 异常片段最小间隔

    # ----- 外部接口 -----

    def feed_packet(self, pkt: RtpPacket) -> None:
        """将 RTP 包送入对应的缓冲。"""
        media_kind = "unknown"
        if pkt.payload_type in (0, 8, 9, 10, 11):
            media_kind = "audio"
        elif 96 <= pkt.payload_type <= 127 or pkt.payload_type in (26, 28, 31, 32, 34):
            media_kind = "video"

        if media_kind == "video":
            self._buffer_video_frame(pkt)
        elif media_kind == "audio":
            self._buffer_audio_packet(pkt)

    def feed_decoded_video(self, ssrc: int, frame_rgb: np.ndarray,
                           width: int, height: int) -> None:
        """送入解码后的 RGB 帧供 AI 分析。"""
        if ssrc not in self._last_rgb_frames:
            self._last_rgb_frames[ssrc] = []
        self._last_rgb_frames[ssrc].append((time.time(), frame_rgb.copy()))
        # 保留最近 3 秒（~90 帧 @ 30fps）
        if len(self._last_rgb_frames[ssrc]) > 90:
            self._last_rgb_frames[ssrc].pop(0)

    def feed_decoded_audio(self, ssrc: int, pcm: np.ndarray,
                           sample_rate: int, num_channels: int = 1) -> None:
        """送入解码后的 PCM 供 AI 分析。"""
        if ssrc not in self._last_pcm:
            self._last_pcm[ssrc] = []
        self._last_pcm[ssrc].append((time.time(), pcm.copy(), sample_rate))
        # 保留最近 3 秒（~150 包 @ 20ms 帧）
        if len(self._last_pcm[ssrc]) > 150:
            self._last_pcm[ssrc].pop(0)

    def get_last_rgb_frames(self, ssrc: int, count: int = 10) -> List:
        """获取最近 N 帧 RGB 数据供 AI 分析。"""
        frames = self._last_rgb_frames.get(ssrc, [])
        if not frames:
            return []
        from .content_buffer import VideoFrame
        result = []
        for ts, data in frames[-count:]:
            result.append(VideoFrame(
                timestamp=ts,
                data=data,
                width=data.shape[1],
                height=data.shape[0],
                pts=0,
            ))
        return result

    def get_last_pcm(self, ssrc: int, duration_sec: float = 5.0) -> Optional[np.ndarray]:
        """获取最近 duration_sec 秒的 PCM 供 AI 分析。"""
        pcm_list = self._last_pcm.get(ssrc, [])
        if not pcm_list:
            return None
        # 取最近的 ~duration_sec 秒
        recent = pcm_list[-int(duration_sec * 50):]  # 假设 20ms 一帧 = 50fps
        if not recent:
            return None
        return np.concatenate([p[1] for p in recent])

    def get_sample_rate(self, ssrc: int) -> int:
        pcm_list = self._last_pcm.get(ssrc, [])
        if pcm_list:
            return pcm_list[-1][2]
        return 16000

    def has_decoded_video(self, ssrc: int) -> bool:
        return len(self._last_rgb_frames.get(ssrc, [])) > 0

    def has_decoded_audio(self, ssrc: int) -> bool:
        return len(self._last_pcm.get(ssrc, [])) > 0

    # ----- MOS 告警触发 -----

    def on_mos_alert(self, analyzer: StreamAnalyzer, snap: StreamMetrics) -> Optional[str]:
        """质量下降时触发，返回保存的文件路径。"""
        ssrc = analyzer.info.ssrc
        now = time.time()
        last = self._last_capture.get(ssrc, 0.0)
        if now - last < self._min_interval:
            return None
        self._last_capture[ssrc] = now

        if analyzer.info.media_kind == "video":
            return self._save_screenshot(analyzer, snap)
        elif analyzer.info.media_kind == "audio":
            return self._save_audio_clip(analyzer, snap)
        return None

    # ----- AI 内容异常触发 -----

    def on_content_anomaly(self, analyzer: StreamAnalyzer,
                           content_buffer: ContentBuffer,
                           ai_result: ContentAnalysisResult) -> Optional[List[str]]:
        """内容异常时保存前后各 1 秒的片段，返回保存的文件路径列表。"""
        ssrc = analyzer.info.ssrc
        now = time.time()
        last = self._last_anomaly_capture.get(ssrc, 0.0)
        if now - last < self._anomaly_min_interval:
            return None
        self._last_anomaly_capture[ssrc] = now

        saved_files: List[str] = []
        ts = time.strftime("%Y%m%d_%H%M%S", time.localtime())
        anomaly_id = f"ssrc_{analyzer.info.ssrc:08X}_{ts}_AIScore{ai_result.anomaly_score:.0f}"

        # 保存异常片段
        if analyzer.info.media_kind == "video" or ai_result.video_anomalies:
            filepath = self._save_anomaly_video(anomaly_id, content_buffer, ai_result)
            if filepath:
                saved_files.append(filepath)

        if analyzer.info.media_kind == "audio" or ai_result.audio_anomalies:
            filepath = self._save_anomaly_audio(anomaly_id, content_buffer, ai_result)
            if filepath:
                saved_files.append(filepath)

        # 保存元数据 JSON
        meta_path = self._save_anomaly_metadata(anomaly_id, analyzer, ai_result)
        if meta_path:
            saved_files.append(meta_path)

        return saved_files

    # ----- 视频 -----

    def _buffer_video_frame(self, pkt: RtpPacket) -> None:
        """尝试从 H.264 RTP 负载中提取 I-frame。"""
        if not pkt.payload:
            return
        payload = pkt.payload
        for i in range(min(len(payload), 50)):
            if payload[i] & 0x1F in (5, 7, 8):  # IDR / SPS / PPS
                self._keyframe_buffer[pkt.ssrc] = payload
                break
        if pkt.marker:
            self._keyframe_buffer[pkt.ssrc] = payload

    def _save_screenshot(self, analyzer: StreamAnalyzer, snap: StreamMetrics) -> Optional[str]:
        """将关键帧保存为 JPEG。"""
        data = self._keyframe_buffer.get(analyzer.info.ssrc)
        if not data:
            logger.warning("No keyframe data for SSRC=0x%08X, cannot capture", analyzer.info.ssrc)
            return None

        try:
            ts = time.strftime("%Y%m%d_%H%M%S", time.localtime())
            filename = f"ssrc_{analyzer.info.ssrc:08X}_{ts}_MOS{snap.mos:.1f}.jpg"
            filepath = self._screenshot_dir / filename
            img = self._decode_h264_frame(data, analyzer)
            if img is not None:
                img.save(str(filepath), "JPEG", quality=85)
                logger.info("Screenshot saved: %s", filepath)
                return str(filepath)
            else:
                h264_path = filepath.with_suffix(".h264")
                h264_path.write_bytes(data)
                logger.info("Raw H.264 saved: %s", h264_path)
                return str(h264_path)
        except Exception as exc:
            logger.exception("Screenshot failed: %s", exc)
            return None

    def _save_anomaly_video(self, anomaly_id: str,
                            content_buffer: ContentBuffer,
                            ai_result: ContentAnalysisResult) -> Optional[str]:
        """保存异常视频片段（前后各 1 秒）。"""
        frames = content_buffer.get_video_window(before=1.0, after=1.0)
        if not frames:
            # 尝试从 capture manager 获取
            ssrc = 0
            # 从 content_buffer 中没有，返回 None
            return None

        try:
            filename = f"{anomaly_id}_VIDEO.mp4"
            filepath = self._anomaly_dir / filename

            # 尝试用 PyAV 保存为 MP4
            if self._save_frames_to_mp4(frames, str(filepath)):
                logger.info("Anomaly video clip saved: %s", filepath)
                return str(filepath)
            else:
                # 退而求其次，保存为连续的 JPEG
                jpg_dir = self._anomaly_dir / anomaly_id
                jpg_dir.mkdir(parents=True, exist_ok=True)
                for i, frame in enumerate(frames):
                    from PIL import Image
                    img = Image.fromarray(frame.data)
                    img.save(str(jpg_dir / f"frame_{i:04d}.jpg"), "JPEG")
                logger.info("Anomaly video frames saved: %s", jpg_dir)
                return str(jpg_dir)
        except Exception as exc:
            logger.exception("Anomaly video save failed: %s", exc)
            return None

    @staticmethod
    def _save_frames_to_mp4(frames, filepath: str) -> bool:
        """使用 PyAV 将帧序列保存为 MP4。"""
        try:
            import av
            width = frames[0].width
            height = frames[0].height
            fps = 30.0

            container = av.open(filepath, 'w')
            stream = container.add_stream('libx264', rate=fps)
            stream.width = width
            stream.height = height
            stream.pix_fmt = 'yuv420p'

            for frame in frames:
                av_frame = av.VideoFrame.from_ndarray(frame.data, format='rgb24')
                for packet in stream.encode(av_frame):
                    container.mux(packet)

            for packet in stream.encode():
                container.mux(packet)

            container.close()
            return True
        except Exception:
            return False

    @staticmethod
    def _decode_h264_frame(data: bytes, analyzer: StreamAnalyzer):
        """尝试使用 PyAV 解码单帧 H.264 数据。"""
        try:
            import av
            codec = av.CodecContext.create("h264", "r")
            annex_b = b'\x00\x00\x00\x01' + data.lstrip(b'\x00\x00\x00\x01\x00\x00\x01')
            packet = av.Packet(annex_b)
            frames = codec.decode(packet)
            for frame in frames:
                img = frame.to_ndarray(format="rgb24")
                from PIL import Image
                return Image.fromarray(img)
        except Exception:
            pass
        return None

    # ----- 音频 -----

    def _buffer_audio_packet(self, pkt: RtpPacket) -> None:
        """缓存音频 RTP 负载。"""
        if pkt.ssrc not in self._audio_buffer:
            self._audio_buffer[pkt.ssrc] = Deque(maxlen=500)
        self._audio_buffer[pkt.ssrc].append(pkt.payload)

    def _save_audio_clip(self, analyzer: StreamAnalyzer, snap: StreamMetrics) -> Optional[str]:
        """将缓存的音频数据保存为 WAV。"""
        buf = self._audio_buffer.get(analyzer.info.ssrc)
        if not buf:
            logger.warning("No audio buffer for SSRC=0x%08X", analyzer.info.ssrc)
            return None

        try:
            ts = time.strftime("%Y%m%d_%H%M%S", time.localtime())
            filename = f"ssrc_{analyzer.info.ssrc:08X}_{ts}_MOS{snap.mos:.1f}.wav"
            filepath = self._recording_dir / filename

            raw_data = b"".join(buf)
            sample_rate, num_channels, pcm_data = self._audio_to_pcm(raw_data, analyzer)
            self._write_wav(str(filepath), pcm_data, sample_rate, num_channels)
            logger.info("Audio clip saved: %s", filepath)
            return str(filepath)
        except Exception as exc:
            logger.exception("Audio capture failed: %s", exc)
            return None

    def _save_anomaly_audio(self, anomaly_id: str,
                            content_buffer: ContentBuffer,
                            ai_result: ContentAnalysisResult) -> Optional[str]:
        """保存异常音频片段（前后各 1 秒）。"""
        pcm = content_buffer.get_audio_window(before=1.0, after=1.0)
        if pcm is None:
            return None

        try:
            filename = f"{anomaly_id}_AUDIO.wav"
            filepath = self._anomaly_dir / filename
            self._write_wav(str(filepath), pcm.astype(np.int16).tobytes(),
                            content_buffer.sample_rate, 1)
            logger.info("Anomaly audio clip saved: %s", filepath)
            return str(filepath)
        except Exception as exc:
            logger.exception("Anomaly audio save failed: %s", exc)
            return None

    def _audio_to_pcm(self, raw_data: bytes, analyzer: StreamAnalyzer) -> Tuple[int, int, bytes]:
        """将音频负载转换为 PCM。"""
        sample_rate = 16000
        num_channels = 1
        if analyzer.info.payload_type in (0, 8):
            sample_rate = 8000
            num_channels = 1
            pcm_data = self._g711_to_pcm(raw_data, is_alaw=(analyzer.info.payload_type == 8))
        elif analyzer.info.payload_type == 9:
            sample_rate = 16000
            num_channels = 1
            pcm_data = raw_data
        else:
            sample_rate = 16000
            num_channels = 1
            pcm_data = raw_data
        return sample_rate, num_channels, pcm_data

    @staticmethod
    def _g711_to_pcm(data: bytes, is_alaw: bool = False) -> bytes:
        """G.711 A-law/u-law 到 16-bit PCM 的简化转换。"""
        result = bytearray()
        for byte in data:
            if is_alaw:
                s = byte ^ 0x55 if (byte & 0x80) else byte
                sample = (s & 0x7F) << 3
                if byte & 0x80:
                    sample = -sample
            else:
                s = ~byte
                seg = (s >> 4) & 0x07
                step = 4 << seg
                sample = step + ((s & 0x0F) * (step >> 3))
                if byte & 0x80:
                    sample = -sample
            result.extend(struct.pack("<h", max(-32768, min(32767, sample))))
        return bytes(result)

    @staticmethod
    def _write_wav(path: str, pcm_data: bytes, sample_rate: int, num_channels: int) -> None:
        """写入标准 WAV 文件头 + PCM 数据。"""
        num_samples = len(pcm_data) // 2
        data_size = num_samples * 2
        file_size = 44 + data_size

        header = struct.pack("<4sI4s4sIHHIIHH4sI",
            b"RIFF", file_size - 8, b"WAVE",
            b"fmt ", 16, 1, num_channels,
            sample_rate, sample_rate * num_channels * 2,
            num_channels * 2, 16,
            b"data", data_size,
        )
        with open(path, "wb") as f:
            f.write(header)
            f.write(pcm_data)

    # ----- 元数据 -----

    def _save_anomaly_metadata(self, anomaly_id: str,
                               analyzer: StreamAnalyzer,
                               ai_result: ContentAnalysisResult) -> Optional[str]:
        """保存异常元数据 JSON，包含根因分析和 RTP 指标。"""
        try:
            meta = {
                "anomaly_id": anomaly_id,
                "timestamp": ai_result.timestamp,
                "time_str": time.strftime("%Y-%m-%d %H:%M:%S",
                                         time.localtime(ai_result.timestamp)),
                "ssrc": f"0x{analyzer.info.ssrc:08X}",
                "media_kind": analyzer.info.media_kind,
                "codec": analyzer.info.codec,
                "source": f"{analyzer.info.source_ip}:{analyzer.info.source_port}",
                "ai_analysis": {
                    "anomaly_score": ai_result.anomaly_score,
                    "audio_score": ai_result.audio_score,
                    "video_score": ai_result.video_score,
                    "audio_anomalies": ai_result.audio_anomalies,
                    "video_anomalies": ai_result.video_anomalies,
                    "details": ai_result.details,
                },
                "root_cause": {
                    "cause": ai_result.root_cause,
                    "confidence": ai_result.confidence,
                },
                "rtp_metrics": {
                    "loss_rate": ai_result.rtp_loss_rate,
                    "jitter_ms": ai_result.rtp_jitter,
                    "mos": ai_result.rtp_mos,
                },
                "stream_summary": analyzer.get_stream_summary(),
            }

            filepath = self._anomaly_dir / f"{anomaly_id}_META.json"
            with open(filepath, "w", encoding="utf-8") as f:
                json.dump(meta, f, indent=2, ensure_ascii=False, default=str)
            logger.info("Anomaly metadata saved: %s", filepath)
            return str(filepath)
        except Exception as exc:
            logger.exception("Metadata save failed: %s", exc)
            return None
