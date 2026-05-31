"""RTP 报文解析模块

按照 RFC 3550 解析 RTP 固定头与扩展头，提供轻量的 Packet/Stream 数据结构。
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import List, Optional, Tuple


RTP_VERSION = 2
RTP_HEADER_MIN_LEN = 12


@dataclass
class RtpPacket:
    """解析后的 RTP 报文。"""

    version: int
    padding: bool
    extension: bool
    csrc_count: int
    marker: bool
    payload_type: int
    sequence: int
    timestamp: int
    ssrc: int
    csrc: List[int] = field(default_factory=list)
    extension_profile: Optional[int] = None
    extension_payload: bytes = b""
    payload: bytes = b""
    arrival_time: float = 0.0
    size: int = 0


def parse_rtp(data: bytes, arrival_time: float = 0.0) -> Optional[RtpPacket]:
    """解析原始 UDP 数据为 RtpPacket，非法数据返回 None。"""
    if len(data) < RTP_HEADER_MIN_LEN:
        return None

    try:
        first, second, seq, ts, ssrc = struct.unpack_from("!BBHII", data, 0)
    except struct.error:
        return None

    version = (first >> 6) & 0x03
    if version != RTP_VERSION:
        return None

    padding = bool((first >> 5) & 0x01)
    extension = bool((first >> 4) & 0x01)
    csrc_count = first & 0x0F
    marker = bool((second >> 7) & 0x01)
    payload_type = second & 0x7F

    offset = RTP_HEADER_MIN_LEN
    csrc: List[int] = []
    if csrc_count:
        if len(data) < offset + csrc_count * 4:
            return None
        csrc = list(struct.unpack_from(f"!{csrc_count}I", data, offset))
        offset += csrc_count * 4

    ext_profile = None
    ext_payload = b""
    if extension:
        if len(data) < offset + 4:
            return None
        ext_profile, ext_len = struct.unpack_from("!HH", data, offset)
        offset += 4
        ext_bytes = ext_len * 4
        if len(data) < offset + ext_bytes:
            return None
        ext_payload = data[offset : offset + ext_bytes]
        offset += ext_bytes

    payload = data[offset:]
    if padding and payload:
        pad_len = payload[-1]
        if 0 < pad_len <= len(payload):
            payload = payload[:-pad_len]

    return RtpPacket(
        version=version,
        padding=padding,
        extension=extension,
        csrc_count=csrc_count,
        marker=marker,
        payload_type=payload_type,
        sequence=seq,
        timestamp=ts,
        ssrc=ssrc,
        csrc=csrc,
        extension_profile=ext_profile,
        extension_payload=ext_payload,
        payload=payload,
        arrival_time=arrival_time,
        size=len(data),
    )


# 常见 RTP 负载类型 -> 媒体类型/名称
_PAYLOAD_TYPE_MAP = {
    0: ("audio", "PCMU"),
    3: ("audio", "GSM"),
    4: ("audio", "G723"),
    8: ("audio", "PCMA"),
    9: ("audio", "G722"),
    10: ("audio", "L16-stereo"),
    11: ("audio", "L16-mono"),
    14: ("audio", "MPEG"),
    15: ("audio", "AAC"),
    26: ("video", "JPEG"),
    28: ("video", "H261"),
    31: ("video", "H263"),
    32: ("video", "MPV"),
    33: ("video", "MP2T"),
    34: ("video", "H263+"),
}

_DYNAMIC_CLUE = {
    "H264": "video",
    "H265": "video",
    "VP8": "video",
    "VP9": "video",
    "AV1": "video",
    "OPUS": "audio",
    "G7221": "audio",
    "G729": "audio",
    "MPEG4-GENERIC": "audio",
    "MP4A-LATM": "audio",
    "MP4V-ES": "video",
}


def guess_media_kind(payload_type: int, codec_name: Optional[str] = None) -> Tuple[str, str]:
    """返回 (kind, codec)。kind 为 'audio'/'video'/'unknown'。"""
    if codec_name:
        upper = codec_name.upper()
        for clue, kind in _DYNAMIC_CLUE.items():
            if clue in upper:
                return kind, codec_name
    if payload_type in _PAYLOAD_TYPE_MAP:
        return _PAYLOAD_TYPE_MAP[payload_type]
    if 96 <= payload_type <= 127:
        return "unknown", f"dynamic-{payload_type}"
    return "unknown", f"pt-{payload_type}"
