"""AI 内容分析模块（本地轻量实现）

使用信号处理和图像处理技术实现本地内容质量分析，无需外部 API。

音频分析：
- 卡顿检测：短时能量突变 + 零交叉率
- 爆破音检测：波峰因子（Peak-to-RMS ratio）+ 短时能量尖峰
- 回声检测：自相关函数延迟峰值

视频分析：
- 花屏检测：高频能量占比 + 边缘密度突变
- 绿屏检测：绿色通道占比 > 70%
- 画面冻结：帧间差分（MSE）< 阈值
- 马赛克检测：块效应（8x8 DCT 高频块占比）

根因定位：
- 关联 RTP 层丢包率、抖动、MOS
- 高丢包 + 视频花屏 → 网络丢包导致的图像损坏
- 低 MOS + 音频卡顿 → 网络问题
- 无丢包 + 绿屏/冻结 → 采集端/编码端问题
"""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple

import numpy as np
from scipy import signal
from scipy.ndimage import sobel

from .content_buffer import ContentAnalysisResult
from .quality import StreamMetrics

logger = logging.getLogger(__name__)


# ========== 配置 ==========

@dataclass
class AiConfig:
    audio_analysis_interval: float = 5.0
    video_analysis_interval: float = 5.0
    audio_anomaly_threshold: float = 30.0
    video_anomaly_threshold: float = 30.0
    overall_anomaly_threshold: float = 40.0
    # 音频
    stutter_energy_drop: float = 0.6  # 能量下降 60% 认为卡顿
    pop_peak_factor: float = 10.0  # 峰值/RMS > 10 认为爆破音
    echo_threshold: float = 0.3  # 自相关峰值 > 0.3 认为有回声
    # 视频
    freeze_mse_threshold: float = 100.0  # MSE < 100 认为冻结
    green_screen_ratio: float = 0.65  # 绿色通道占比 > 65% 认为绿屏
    block_effect_ratio: float = 0.3  # 高频块占比 > 30% 认为马赛克
    high_freq_ratio: float = 0.4  # 高频能量占比 > 40% 认为花屏


DEFAULT_CONFIG = AiConfig()


# ========== 音频分析器 ==========

class AudioAnalyzer:
    """音频内容质量分析器。"""

    def __init__(self, config: AiConfig = DEFAULT_CONFIG) -> None:
        self._config = config

    def analyze(self, pcm: np.ndarray, sample_rate: int) -> Tuple[float, List[str], dict]:
        """分析 PCM 音频数据，返回 (异常分数, 异常类型列表, 详细数据)。"""
        if pcm is None or len(pcm) < sample_rate:
            return 0.0, [], {}

        # 转换为 float32
        audio = pcm.astype(np.float32) / 32768.0

        anomalies: List[str] = []
        details: dict = {}
        score = 0.0

        # 分帧（20ms 一帧）
        frame_len = int(0.02 * sample_rate)
        hop_len = int(0.01 * sample_rate)
        frames = np.array([
            audio[i:i + frame_len]
            for i in range(0, len(audio) - frame_len, hop_len)
        ])

        if len(frames) < 5:
            return 0.0, [], {}

        # 1. 短时能量
        energy = np.mean(frames ** 2, axis=1)
        energy = energy / (np.max(energy) + 1e-10)
        details["energy_mean"] = float(np.mean(energy))

        # 2. 零交叉率
        zcr = np.mean(np.abs(np.diff(np.sign(frames), axis=1)), axis=1) / 2
        details["zcr_mean"] = float(np.mean(zcr))

        # 3. 卡顿检测：能量突然下降
        energy_diff = np.diff(energy)
        stutter_count = np.sum(energy_diff < -self._config.stutter_energy_drop)
        stutter_score = min(100.0, stutter_count * 15.0)
        if stutter_score > self._config.audio_anomaly_threshold:
            anomalies.append("stutter")
            details["stutter_score"] = stutter_score
            details["stutter_count"] = int(stutter_count)
            score = max(score, stutter_score)

        # 4. 爆破音检测：峰值因子
        rms = np.sqrt(np.mean(frames ** 2, axis=1) + 1e-10)
        peaks = np.max(np.abs(frames), axis=1)
        peak_factor = peaks / (rms + 1e-10)
        pop_count = np.sum(peak_factor > self._config.pop_peak_factor)
        pop_score = min(100.0, pop_count * 12.0)
        if pop_score > self._config.audio_anomaly_threshold:
            anomalies.append("pop")
            details["pop_score"] = pop_score
            details["pop_count"] = int(pop_count)
            details["peak_factor_max"] = float(np.max(peak_factor))
            score = max(score, pop_score)

        # 5. 回声检测：自相关
        try:
            autocorr = np.correlate(audio, audio, mode="full")
            autocorr = autocorr[len(autocorr) // 2:]
            autocorr = autocorr / (autocorr[0] + 1e-10)
            # 查找 50ms-300ms 范围内的峰值
            min_delay = int(0.05 * sample_rate)
            max_delay = int(0.3 * sample_rate)
            if max_delay < len(autocorr):
                echo_peak = np.max(autocorr[min_delay:max_delay])
                echo_score = echo_peak * 100.0
                if echo_score > self._config.echo_threshold * 100:
                    anomalies.append("echo")
                    details["echo_score"] = echo_score
                    details["echo_delay_ms"] = int(np.argmax(autocorr[min_delay:max_delay]) / sample_rate * 1000)
                    score = max(score, echo_score)
        except Exception:
            pass

        # 6. 静音检测
        silence_ratio = np.sum(energy < 0.01) / len(energy)
        details["silence_ratio"] = float(silence_ratio)

        details["anomaly_score"] = float(score)
        return score, anomalies, details


# ========== 视频分析器 ==========

class VideoAnalyzer:
    """视频内容质量分析器。"""

    def __init__(self, config: AiConfig = DEFAULT_CONFIG) -> None:
        self._config = config
        self._last_frame: Optional[np.ndarray] = None

    def analyze(self, frames: List) -> Tuple[float, List[str], dict]:
        """分析视频帧列表，返回 (异常分数, 异常类型列表, 详细数据)。"""
        if not frames or len(frames) < 2:
            return 0.0, [], {}

        anomalies: List[str] = []
        details: dict = {}
        score = 0.0

        # 取最新帧
        latest = frames[-1].data.astype(np.float32)
        prev = frames[-2].data.astype(np.float32) if len(frames) >= 2 else None

        height, width = latest.shape[:2]
        details["resolution"] = f"{width}x{height}"

        # 1. 冻结检测：帧间 MSE
        if prev is not None and prev.shape == latest.shape:
            mse = np.mean((latest - prev) ** 2)
            details["frame_mse"] = float(mse)
            if mse < self._config.freeze_mse_threshold:
                freeze_score = 100.0 - (mse / self._config.freeze_mse_threshold) * 100.0
                freeze_score = max(0.0, freeze_score)
                anomalies.append("freeze")
                details["freeze_score"] = freeze_score
                score = max(score, freeze_score)

        # 2. 绿屏检测
        r_channel = latest[:, :, 0]
        g_channel = latest[:, :, 1]
        b_channel = latest[:, :, 2]
        total = r_channel + g_channel + b_channel + 1e-10
        g_ratio = np.mean(g_channel / total)
        details["green_ratio"] = float(g_ratio)
        if g_ratio > self._config.green_screen_ratio:
            green_score = (g_ratio - self._config.green_screen_ratio) / (1.0 - self._config.green_screen_ratio) * 100.0
            green_score = max(0.0, min(100.0, green_score))
            anomalies.append("green_screen")
            details["green_screen_score"] = green_score
            score = max(score, green_score)

        # 3. 花屏检测：高频能量占比
        gray = np.mean(latest, axis=2)
        fft = np.fft.fft2(gray)
        fft_shift = np.fft.fftshift(fft)
        magnitude = np.abs(fft_shift)

        center_y, center_x = height // 2, width // 2
        low_freq_radius = min(height, width) // 8
        yy, xx = np.mgrid[0:height, 0:width]
        dist_from_center = np.sqrt((yy - center_y) ** 2 + (xx - center_x) ** 2)

        low_freq_mask = dist_from_center < low_freq_radius
        high_freq_mask = ~low_freq_mask

        low_freq_energy = np.sum(magnitude[low_freq_mask])
        high_freq_energy = np.sum(magnitude[high_freq_mask])
        total_energy = low_freq_energy + high_freq_energy + 1e-10
        high_freq_ratio = high_freq_energy / total_energy

        details["high_freq_ratio"] = float(high_freq_ratio)
        if high_freq_ratio > self._config.high_freq_ratio:
            glitch_score = (high_freq_ratio - self._config.high_freq_ratio) / (1.0 - self._config.high_freq_ratio) * 100.0
            glitch_score = max(0.0, min(100.0, glitch_score))
            anomalies.append("glitch")
            details["glitch_score"] = glitch_score
            score = max(score, glitch_score)

        # 4. 马赛克检测：块效应（8x8 DCT）
        block_size = 8
        if height >= block_size and width >= block_size:
            h_blocks = height // block_size
            w_blocks = width // block_size
            block_high_freq = 0
            total_blocks = 0

            for by in range(h_blocks):
                for bx in range(w_blocks):
                    block = gray[by * block_size:(by + 1) * block_size,
                                bx * block_size:(bx + 1) * block_size]
                    if block.shape == (block_size, block_size):
                        dct = np.fft.fft2(block)
                        dct_mag = np.abs(dct)
                        # 高频区域：左上 1x1 之外的区域
                        hf_energy = np.sum(dct_mag[2:, 2:])
                        total_block_energy = np.sum(dct_mag) + 1e-10
                        if hf_energy / total_block_energy > 0.3:
                            block_high_freq += 1
                        total_blocks += 1

            block_ratio = block_high_freq / max(total_blocks, 1)
            details["block_high_freq_ratio"] = float(block_ratio)
            if block_ratio > self._config.block_effect_ratio:
                mosaic_score = (block_ratio - self._config.block_effect_ratio) / (1.0 - self._config.block_effect_ratio) * 100.0
                mosaic_score = max(0.0, min(100.0, mosaic_score))
                anomalies.append("mosaic")
                details["mosaic_score"] = mosaic_score
                score = max(score, mosaic_score)

        # 5. 边缘密度（补充花屏检测）
        try:
            edges = sobel(gray)
            edge_density = np.mean(edges > np.percentile(edges, 95))
            details["edge_density"] = float(edge_density)
            if edge_density > 0.25 and high_freq_ratio > 0.35:
                anomalies.append("corrupted")
                score = max(score, 85.0)
        except Exception:
            pass

        details["anomaly_score"] = float(score)
        self._last_frame = latest
        return score, anomalies, details


# ========== 根因分析器 ==========

class RootCauseAnalyzer:
    """关联 RTP 指标进行根因定位。"""

    @staticmethod
    def analyze(ai_result: ContentAnalysisResult,
                latest_metrics: Optional[StreamMetrics]) -> Tuple[str, float]:
        """分析根因，返回 (根因描述, 置信度)。"""

        has_rtp_issue = False
        if latest_metrics:
            has_rtp_issue = (
                latest_metrics.loss_rate > 0.01 or  # > 1% 丢包
                latest_metrics.jitter > 50.0 or     # > 50ms 抖动
                latest_metrics.mos < 3.5            # MOS 偏低
            )

        has_content_issue = ai_result.is_anomaly
        audio_issues = ai_result.audio_anomalies
        video_issues = ai_result.video_anomalies

        confidence = 0.0
        root_cause = "unknown"

        if has_rtp_issue and has_content_issue:
            # 网络问题导致内容异常
            if "glitch" in video_issues or "mosaic" in video_issues:
                root_cause = "network_packet_loss_causing_video_corruption"
                confidence = 0.9
            elif "stutter" in audio_issues or "pop" in audio_issues:
                root_cause = "network_jitter_causing_audio_artifact"
                confidence = 0.85
            else:
                root_cause = "network_quality_issue"
                confidence = 0.8
        elif has_content_issue and not has_rtp_issue:
            # 采集/编码端问题
            if "green_screen" in video_issues:
                root_cause = "capture_source_failure_green_screen"
                confidence = 0.95
            elif "freeze" in video_issues:
                root_cause = "encoder_freeze_or_capture_hang"
                confidence = 0.85
            elif "echo" in audio_issues:
                root_cause = "acoustic_echo_in_capture"
                confidence = 0.8
            elif "pop" in audio_issues:
                root_cause = "audio_clipping_or_microphone_issue"
                confidence = 0.7
            else:
                root_cause = "content_source_issue"
                confidence = 0.6
        elif has_rtp_issue and not has_content_issue:
            # 网络有问题但内容还可以
            root_cause = "network_degradation_no_content_impact"
            confidence = 0.7
        else:
            root_cause = "normal"
            confidence = 0.95

        return root_cause, confidence


# ========== 主分析器 ==========

class ContentAIAnalyzer:
    """AI 内容分析主入口。"""

    ANALYSIS_INTERVAL = 5.0  # 每 5 秒分析一次

    def __init__(self, ssrc: int, media_kind: str,
                 config: AiConfig = DEFAULT_CONFIG) -> None:
        self._ssrc = ssrc
        self._media_kind = media_kind
        self._config = config

        self._audio_analyzer = AudioAnalyzer(config)
        self._video_analyzer = VideoAnalyzer(config)
        self._root_cause = RootCauseAnalyzer()

        self._last_analysis: float = 0.0
        self._results: List[ContentAnalysisResult] = []

    def analyze(self, content_buffer,
                latest_metrics: Optional[StreamMetrics]) -> Optional[ContentAnalysisResult]:
        """执行一次分析（如果到了分析间隔）。"""

        now = time.time()
        if now - self._last_analysis < self.ANALYSIS_INTERVAL:
            return None
        self._last_analysis = now

        audio_score = 0.0
        video_score = 0.0
        audio_anomalies: List[str] = []
        video_anomalies: List[str] = []
        details: dict = {}

        # 音频分析
        if self._media_kind in ("audio", "unknown") and content_buffer.has_audio:
            pcm = content_buffer.get_latest_audio(duration_sec=5.0)
            if pcm is not None and len(pcm) > 0:
                audio_score, audio_anomalies, audio_details = self._audio_analyzer.analyze(
                    pcm, content_buffer.sample_rate
                )
                details["audio"] = audio_details

        # 视频分析
        if self._media_kind in ("video", "unknown") and content_buffer.has_video:
            frames = content_buffer.get_latest_video_frames(count=10)
            if frames:
                video_score, video_anomalies, video_details = self._video_analyzer.analyze(frames)
                details["video"] = video_details

        # 综合评分
        overall_score = max(audio_score, video_score)
        is_anomaly = overall_score > self._config.overall_anomaly_threshold

        # 关联 RTP 指标
        rtp_loss = latest_metrics.loss_rate if latest_metrics else 0.0
        rtp_jitter = latest_metrics.jitter if latest_metrics else 0.0
        rtp_mos = latest_metrics.mos if latest_metrics else 4.5

        result = ContentAnalysisResult(
            timestamp=now,
            anomaly_score=overall_score,
            is_anomaly=is_anomaly,
            audio_score=audio_score,
            video_score=video_score,
            audio_anomalies=audio_anomalies,
            video_anomalies=video_anomalies,
            details=details,
            rtp_loss_rate=rtp_loss,
            rtp_jitter=rtp_jitter,
            rtp_mos=rtp_mos,
        )

        # 根因分析
        root_cause, confidence = self._root_cause.analyze(result, latest_metrics)
        result.root_cause = root_cause
        result.confidence = confidence

        # 保存最近 10 条结果
        self._results.append(result)
        if len(self._results) > 10:
            self._results.pop(0)

        if is_anomaly:
            logger.warning(
                "Content anomaly detected SSRC=0x%08X score=%.1f "
                "audio=%s video=%s root_cause=%s",
                self._ssrc, overall_score,
                ",".join(audio_anomalies) or "none",
                ",".join(video_anomalies) or "none",
                root_cause,
            )

        return result

    def get_latest_result(self) -> Optional[ContentAnalysisResult]:
        return self._results[-1] if self._results else None

    def get_all_results(self) -> List[ContentAnalysisResult]:
        return list(self._results)
