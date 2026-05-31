"""质量指标计算模块

- MOS-LQ 估算（基于丢包、抖动、端到端延迟的 E-Model 简化版）
- 丢包率（基于 RTP sequence gap）
- 抖动（RFC 3550 Jitter 算法）
- 延迟（基于 arrival_time 与 RTP timestamp 的相对差值）
- 视频：帧率（基于 marker/pts）、码率（字节数/时间窗口）、分辨率（从 H.264 SPS 或容器中探测）
"""

from __future__ import annotations

import math
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Deque, Dict, List, Optional, Tuple

from .rtp_parser import RtpPacket, guess_media_kind


# ========== 数据结构 ==========

@dataclass
class StreamMetrics:
    """1 秒粒度的质量快照。"""

    timestamp: float
    packet_count: int = 0
    byte_count: int = 0
    lost_packets: int = 0
    loss_rate: float = 0.0
    jitter: float = 0.0
    delay: float = 0.0
    mos: float = 4.5
    fps: float = 0.0
    bitrate: float = 0.0
    width: int = 0
    height: int = 0
    codec: str = ""


@dataclass
class StreamInfo:
    ssrc: int
    media_kind: str
    codec: str
    source_ip: str
    source_port: int
    payload_type: int
    created_at: float = field(default_factory=time.time)
    last_seen: float = 0.0
    total_packets: int = 0
    total_lost: int = 0
    total_bytes: int = 0


# ========== 核心计算 ==========

class StreamAnalyzer:
    """单条 RTP 流的实时质量分析器。

    维护一个 10 秒滚动窗口，每秒产出一个 StreamMetrics 快照。
    """

    WINDOW_SECONDS = 10
    SNAPSHOT_INTERVAL = 1.0  # 每秒

    def __init__(self, ssrc: int, media_kind: str = "unknown", codec: str = "",
                 source_ip: str = "", source_port: int = 0, payload_type: int = 0) -> None:
        self.info = StreamInfo(
            ssrc=ssrc,
            media_kind=media_kind,
            codec=codec,
            source_ip=source_ip,
            source_port=source_port,
            payload_type=payload_type,
        )
        self._packets_window: Deque[Tuple[float, int]] = deque()  # (time, size)
        self._metrics: Deque[StreamMetrics] = deque()
        self._cur_pkts = 0
        self._cur_bytes = 0
        self._cur_lost = 0
        self._last_snapshot = 0.0
        self._last_seq: Optional[int] = None
        self._last_ts: Optional[int] = None
        self._last_arrival: float = 0.0
        self._jitter = 0.0
        self._cum_delay = 0.0
        self._delay_samples = 0
        self._first_ts: Optional[int] = None
        self._first_arrival: float = 0.0
        # 视频专用
        self._cur_frames = 0
        self._last_frame_ts: Optional[int] = None
        self._cur_width = 0
        self._cur_height = 0
        self._resolved = False

    # ----- 外部接口 -----

    def feed(self, pkt: RtpPacket) -> Optional[StreamMetrics]:
        """喂入一个 RTP 包，返回新生成的快照（如果到了下一秒）。"""
        now = time.time()
        self.info.last_seen = now
        self.info.total_packets += 1
        self.info.total_bytes += pkt.size

        # 丢包 & 抖动
        if self._last_seq is not None:
            diff = (pkt.sequence - self._last_seq) & 0xFFFF
            if diff > 1:
                self._cur_lost += (diff - 1)
                self.info.total_lost += (diff - 1)
            self._update_jitter(pkt, now)
        self._last_seq = pkt.sequence

        # 延迟估算：相对第一个包
        if self._first_ts is None:
            self._first_ts = pkt.timestamp
            self._first_arrival = now
        else:
            # 使用 90kHz 作为默认时钟（视频），音频会在 push 时修正
            ts_diff = pkt.timestamp - self._first_ts
            # 防止回绕
            if ts_diff < -(1 << 31):
                ts_diff += (1 << 32)
            arrival_diff = (now - self._first_arrival) * 90000.0
            delay_ms = (arrival_diff - ts_diff) / 90.0  # ms
            if delay_ms > 0:
                self._cum_delay += delay_ms
                self._delay_samples += 1

        # 视频帧统计（基于 marker 位或 timestamp 变化）
        if self.info.media_kind == "video":
            if self._last_frame_ts is None or pkt.timestamp != self._last_frame_ts:
                self._cur_frames += 1
                self._last_frame_ts = pkt.timestamp
            self._probe_resolution(pkt)

        self._cur_pkts += 1
        self._cur_bytes += pkt.size

        # 尝试探测媒体类型
        if self.info.media_kind == "unknown" or not self.info.codec:
            kind, codec = guess_media_kind(pkt.payload_type)
            if kind != "unknown":
                self.info.media_kind = kind
            if not self.info.codec and codec:
                self.info.codec = codec

        # 生成快照
        if self._last_snapshot == 0.0:
            self._last_snapshot = now
        elif now - self._last_snapshot >= self.SNAPSHOT_INTERVAL:
            return self._make_snapshot(now)
        return None

    def _make_snapshot(self, now: float) -> StreamMetrics:
        interval = now - self._last_snapshot
        pkt_cnt = self._cur_pkts
        byte_cnt = self._cur_bytes
        lost = self._cur_lost
        loss_rate = lost / max(lost + pkt_cnt, 1)
        jitter_ms = self._jitter / 1000.0  # 转换为 ms

        avg_delay = (self._cum_delay / max(self._delay_samples, 1)) if self._delay_samples > 0 else 0.0

        mos = self._calc_mos(loss_rate, jitter_ms, avg_delay)

        fps = self._cur_frames / interval if interval > 0 else 0.0
        bitrate = (byte_cnt * 8) / interval / 1000.0 if interval > 0 else 0.0  # kbps

        snap = StreamMetrics(
            timestamp=now,
            packet_count=pkt_cnt,
            byte_count=byte_cnt,
            lost_packets=lost,
            loss_rate=loss_rate,
            jitter=jitter_ms,
            delay=avg_delay,
            mos=mos,
            fps=fps,
            bitrate=bitrate,
            width=self._cur_width,
            height=self._cur_height,
            codec=self.info.codec,
        )

        self._metrics.append(snap)
        while self._metrics and (now - self._metrics[0].timestamp) > self.WINDOW_SECONDS:
            self._metrics.popleft()

        # 重置
        self._cur_pkts = 0
        self._cur_bytes = 0
        self._cur_lost = 0
        self._cur_frames = 0
        self._last_snapshot = now
        return snap

    def _update_jitter(self, pkt: RtpPacket, arrival: float) -> None:
        """RFC 3550 抖动算法（单位：RTP 时钟周期，默认 90kHz）。"""
        if self._last_ts is not None:
            transit = (arrival - self._last_arrival) * 90000.0 - (pkt.timestamp - self._last_ts)
            transit = abs(transit)
            if self._jitter == 0:
                self._jitter = transit
            else:
                self._jitter += (transit - self._jitter) / 16.0
        self._last_ts = pkt.timestamp
        self._last_arrival = arrival

    @staticmethod
    def _calc_mos(loss_rate: float, jitter_ms: float, delay_ms: float) -> float:
        """简化 E-Model 计算 MOS-LQ（1-5 分）。

        R = 94.2 - Ie - Id
        MOS = 1 + (0.035 * R) + (7 * 1e-6 * R * (R-60) * (100-R))
        """
        # 丢包影响系数
        ie = loss_rate * 190.0
        # 延迟影响
        id = 0.0
        if delay_ms > 150:
            id = (delay_ms - 150) * 0.02
        # 抖动影响（每 10ms 抖动扣 1 分）
        ij = jitter_ms * 0.1

        r_score = max(0.0, 94.2 - ie - id - ij)
        r_score = min(100.0, r_score)

        # R 到 MOS 转换（ITU-T G.107）
        if r_score < 0:
            mos = 1.0
        elif r_score > 100:
            mos = 4.5
        else:
            mos = 1.0 + 0.035 * r_score + 7e-6 * r_score * (r_score - 60) * (100 - r_score)
        return max(1.0, min(4.5, mos))

    def _probe_resolution(self, pkt: RtpPacket) -> None:
        """尝试从 RTP 负载中解析视频分辨率。

        支持 H.264 SPS 解析（NAL type 7）。对其他编解码器暂不做深度解析。
        """
        if self._resolved or not pkt.payload:
            return
        payload = pkt.payload
        # 跳过 RTP 帧头的 FU indicator/FU header（H.264 分片）
        # 简化：直接在 payload 中搜索 "sps" 起始码
        try:
            if self.info.codec.upper() in ("H264", "AVC") or self.info.payload_type in (96, 99, 105):
                w, h = self._parse_h264_sps(payload)
                if w and h:
                    self._cur_width = w
                    self._cur_height = h
                    self._resolved = True
        except Exception:
            pass

    @staticmethod
    def _parse_h264_sps(data: bytes) -> Tuple[int, int]:
        """极简 H.264 SPS 分辨率解析（仅支持常见 Profile）。"""
        # 查找 SPS NAL (type 7)
        # 起始码 0x00000001 或 0x000001
        import re as _re
        # 搜索起始码后的 NAL
        idx = 0
        while idx < len(data) - 5:
            # 找 00 00 00 01 或 00 00 01
            if data[idx:idx+4] == b'\x00\x00\x00\x01':
                start = idx + 4
            elif data[idx:idx+3] == b'\x00\x00\x01':
                start = idx + 3
            else:
                idx += 1
                continue
            if start < len(data):
                nal_type = data[start] & 0x1F
                if nal_type == 7:  # SPS
                    sps = data[start+1:]
                    try:
                        w, h = _parse_sps_simple(sps)
                        if w and h:
                            return w, h
                    except Exception:
                        pass
            idx = start + 1
        return 0, 0

    # ----- 查询接口 -----

    def get_window_metrics(self) -> List[StreamMetrics]:
        """返回当前窗口内的所有快照（最新在前）。"""
        return list(reversed(self._metrics))

    def get_latest_metrics(self) -> Optional[StreamMetrics]:
        return self._metrics[-1] if self._metrics else None

    def get_stream_summary(self) -> Dict:
        """汇总统计，用于报表。"""
        snaps = list(self._metrics)
        if not snaps:
            return {}
        mos_vals = [s.mos for s in snaps]
        loss_vals = [s.loss_rate for s in snaps]
        jitter_vals = [s.jitter for s in snaps]
        delay_vals = [s.delay for s in snaps]
        return {
            "ssrc": f"0x{self.info.ssrc:08X}",
            "media_kind": self.info.media_kind,
            "codec": self.info.codec,
            "source": f"{self.info.source_ip}:{self.info.source_port}",
            "duration_sec": len(snaps),
            "total_packets": self.info.total_packets,
            "total_lost": self.info.total_lost,
            "total_loss_rate": self.info.total_lost / max(self.info.total_packets, 1),
            "avg_mos": sum(mos_vals) / len(mos_vals),
            "min_mos": min(mos_vals),
            "max_mos": max(mos_vals),
            "avg_loss": sum(loss_vals) / len(loss_vals),
            "avg_jitter_ms": sum(jitter_vals) / len(jitter_vals),
            "avg_delay_ms": sum(delay_vals) / len(delay_vals),
            "fps": self._cur_frames,
            "bitrate_kbps": (self.info.total_bytes * 8) / max(
                (time.time() - self.info.created_at), 1) / 1000.0,
            "resolution": f"{self._cur_width}x{self._cur_height}" if self._cur_width else "N/A",
        }


def _parse_sps_simple(sps: bytes) -> Tuple[int, int]:
    """极简 SPS 解析：从 profile_id 推断 level，通过 golomb 编码读取宽高。

    仅做最佳努力解析，不是完整的 H.264 解码器。
    """
    if len(sps) < 4:
        return 0, 0

    bits = _BitReader(sps)
    profile_idc = bits.read_u(8)
    bits.skip(8)  # constraint + reserved
    level_idc = bits.read_u(8)
    seq_id = bits.read_ue()
    if profile_idc in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135):
        chroma_idc = bits.read_ue()
        if chroma_idc == 3:
            bits.skip(1)
        bits.read_ue()  # bit_depth_luma
        bits.read_ue()  # bit_depth_chroma
        bits.skip(1)  # transform_bypass
        if bits.read_u(1):
            bits.read_ue()  # scaling matrix
    bits.read_ue()  # log2_max_frame_num
    pic_order_cnt_type = bits.read_ue()
    if pic_order_cnt_type == 0:
        bits.read_ue()
    elif pic_order_cnt_type == 2:
        pass
    bits.read_ue()  # max_num_ref_frames
    bits.skip(1)  # gaps_in_frame_num
    pic_width = (bits.read_ue() + 1) * 16
    frame_mb = bits.read_ue() + 1
    pic_height = frame_mb * 16
    return pic_width, pic_height


class _BitReader:
    """极简位读取器（仅支持 SPS 所需操作）。"""

    def __init__(self, data: bytes) -> None:
        self._data = data
        self._byte = 0
        self._bit = 0

    def read_u(self, n: int) -> int:
        val = 0
        for _ in range(n):
            val = (val << 1) | self._read_bit()
        return val

    def skip(self, n: int) -> None:
        for _ in range(n):
            self._read_bit()

    def read_ue(self) -> int:
        """unsigned Exp-Golomb。"""
        leading = 0
        while not self._read_bit() and leading < 32:
            leading += 1
        if leading == 0:
            return 0
        val = 1 << leading
        for _ in range(leading):
            val = (val << 1) | self._read_bit()
        return val - 1

    def _read_bit(self) -> int:
        if self._byte >= len(self._data):
            return 0
        byte_val = self._data[self._byte]
        bit = (byte_val >> (7 - self._bit)) & 1
        self._bit += 1
        if self._bit >= 8:
            self._bit = 0
            self._byte += 1
        return bit
