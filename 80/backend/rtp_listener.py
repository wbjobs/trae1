"""RTP 网络监听模块

使用 asyncio 的 UDP transport 被动监听指定端口范围（默认 5000-6000），
将接收到的原始数据交给 rtp_parser 解析，然后通过回调推送到上层。

支持 SRTP 解密：如果 SrtpManager 中有对应 SSRC 的密钥上下文，
自动解密 SRTP 负载并验证认证标签，再将明文 RTP 交给解析器。
"""

from __future__ import annotations

import asyncio
import logging
import socket
import struct
import time
from typing import Awaitable, Callable, Dict, List, Optional

from .rtp_parser import RtpPacket, parse_rtp
from .srtp import SrtpManager

logger = logging.getLogger(__name__)


PacketCallback = Callable[[RtpPacket, str, int], Awaitable[None]]


class RtpListenerProtocol:
    """asyncio DatagramProtocol 实现，支持 SRTP 解密。"""

    def __init__(
        self,
        port: int,
        on_packet: PacketCallback,
        loop: asyncio.AbstractEventLoop,
        srtp_manager: Optional[SrtpManager] = None,
    ) -> None:
        self._port = port
        self._on_packet = on_packet
        self._loop = loop
        self._srtp = srtp_manager
        self._transport: Optional[asyncio.DatagramTransport] = None
        self._paused = False

    def connection_made(self, transport: asyncio.DatagramTransport) -> None:
        self._transport = transport
        logger.info("RTP listener started on UDP port %d", self._port)

    def connection_lost(self, exc: Optional[Exception]) -> None:
        logger.info("RTP listener on port %d closed: %s", self._port, exc)

    def datagram_received(self, data: bytes, addr: tuple) -> None:
        if self._paused:
            return

        arrival = time.time()
        host, port = addr

        # 尝试 SRTP 解密（如果有配置）
        rtp_data = data
        if self._srtp is not None:
            try:
                rtp_data = self._try_srtp(data)
            except Exception as exc:
                logger.debug("SRTP processing failed for %s:%d: %s", host, port, exc)

        if rtp_data is None:
            return

        pkt = parse_rtp(rtp_data, arrival)
        if pkt is None:
            return

        # 非阻塞调度到事件循环
        asyncio.run_coroutine_threadsafe(
            self._on_packet(pkt, host, port), self._loop
        )

    def _try_srtp(self, data: bytes) -> Optional[bytes]:
        """尝试 SRTP 解密。

        返回解密后的 RTP 数据，如果不需要/不支持 SRTP 则返回原始数据。
        如果是 SRTP 但解密失败，返回 None。
        """
        if len(data) < 12:
            return data

        # 提取 SSRC（始终为明文，RFC 3711）
        try:
            ssrc = struct.unpack_from("!I", data, 8)[0]
        except struct.error:
            return data

        if not self._srtp.has_context(ssrc):
            return data

        # 尝试 SRTP 解密 + 验证
        result, ctx = self._srtp.process_packet(data)
        if result is not None:
            return result
        # 解密/认证失败，丢弃
        logger.debug(
            "SRTP decryption failed for SSRC=0x%08X (auth_failed=%d, replay=%d)",
            ssrc,
            ctx.packets_auth_failed if ctx else 0,
            ctx.packets_replay_dropped if ctx else 0,
        )
        return None

    def error_received(self, exc: Exception) -> None:
        logger.warning("RTP listener port %d error: %s", self._port, exc)

    def pause(self) -> None:
        self._paused = True

    def resume(self) -> None:
        self._paused = False


class RtpListenerManager:
    """管理一组 UDP 端口的监听，支持 SRTP 解密。"""

    def __init__(
        self,
        port_range: range = range(5000, 6001),
        bind_addr: str = "0.0.0.0",
        on_packet: Optional[PacketCallback] = None,
        loop: Optional[asyncio.AbstractEventLoop] = None,
        srtp_manager: Optional[SrtpManager] = None,
    ) -> None:
        self._ports = list(port_range)
        self._bind_addr = bind_addr
        self._on_packet = on_packet or self._default_callback
        self._loop = loop or asyncio.get_event_loop()
        self._srtp = srtp_manager
        self._transports: Dict[int, asyncio.DatagramTransport] = {}
        self._protocols: Dict[int, RtpListenerProtocol] = {}
        self._running = False

    async def _default_callback(self, pkt: RtpPacket, host: str, port: int) -> None:
        logger.debug(
            "RTP pkt ssrc=%x seq=%d ts=%d pt=%d from %s:%d",
            pkt.ssrc, pkt.sequence, pkt.timestamp, pkt.payload_type, host, port,
        )

    async def start(self) -> None:
        """在所有端口上创建 UDP endpoint。"""
        if self._running:
            return
        for port in self._ports:
            try:
                transport, protocol = await self._loop.create_datagram_endpoint(
                    lambda: RtpListenerProtocol(
                        port, self._on_packet, self._loop, self._srtp
                    ),
                    local_addr=(self._bind_addr, port),
                    family=socket.AF_INET,
                )
                self._transports[port] = transport
                self._protocols[port] = protocol
            except OSError as exc:
                logger.warning("无法绑定端口 %d: %s", port, exc)
        self._running = True
        logger.info(
            "RTP listener manager started, %d/%d ports bound (SRTP=%s)",
            len(self._transports), len(self._ports),
            "enabled" if self._srtp else "disabled",
        )

    def stop(self) -> None:
        for transport in self._transports.values():
            transport.close()
        self._transports.clear()
        self._protocols.clear()
        self._running = False
        logger.info("RTP listener manager stopped")

    def pause_all(self) -> None:
        for proto in self._protocols.values():
            proto.pause()

    def resume_all(self) -> None:
        for proto in self._protocols.values():
            proto.resume()

    def set_srtp_manager(self, srtp_manager: SrtpManager) -> None:
        """动态设置/更新 SRTP 管理器（运行时导入新密钥后调用）。"""
        self._srtp = srtp_manager
        for proto in self._protocols.values():
            proto._srtp = srtp_manager

    @property
    def srtp_manager(self) -> Optional[SrtpManager]:
        return self._srtp

    @property
    def active_ports(self) -> List[int]:
        return list(self._transports.keys())

    @property
    def running(self) -> bool:
        return self._running
