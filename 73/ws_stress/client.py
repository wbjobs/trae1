"""WebSocket 客户端 - 异步连接、加入房间、收发消息、自动重连"""

import asyncio
import json
import random
import string
import time
import uuid
from typing import Optional

import aiohttp

from .stats import StatisticsCollector


class WsClient:
    HEARTBEAT_INTERVAL = 30
    MAX_BUFFERED_MESSAGES = 100
    BACKOFF_BASE = 1.0
    BACKOFF_MAX = 30.0

    def __init__(self, url: str, room: str, client_id: str,
                 msg_min: int = 20, msg_max: int = 200,
                 msg_interval: float = 1.0):
        self._url = url
        self._room = room
        self._client_id = client_id
        self._msg_min = msg_min
        self._msg_max = msg_max
        self._msg_interval = msg_interval
        self._session: Optional[aiohttp.ClientSession] = None
        self._ws: Optional[aiohttp.ClientWebSocketResponse] = None
        self._running = False
        self._send_task: Optional[asyncio.Task] = None
        self._heartbeat_task: Optional[asyncio.Task] = None
        self._pending_messages: dict = {}
        self._connected_event = asyncio.Event()
        self._message_queue: asyncio.Queue = asyncio.Queue(maxsize=self.MAX_BUFFERED_MESSAGES)
        self._is_reconnect = False
        self._current_backoff = self.BACKOFF_BASE
        self._last_recv_ts: float = 0.0

    async def connect(self) -> bool:
        stats = StatisticsCollector()
        try:
            if not self._session or self._session.closed:
                self._session = aiohttp.ClientSession()
            connect_start = time.time()
            self._ws = await self._session.ws_connect(
                self._url,
                timeout=aiohttp.ClientTimeout(total=30),
                heartbeat=self.HEARTBEAT_INTERVAL,
                max_msg_size=4 * 1024 * 1024,
            )
            connect_time = time.time() - connect_start

            join_msg = json.dumps({
                "type": "join",
                "room": self._room,
                "client_id": self._client_id,
            })
            await self._ws.send_str(join_msg)

            self._connected_event.set()
            self._current_backoff = self.BACKOFF_BASE
            self._last_recv_ts = time.time()

            if self._is_reconnect:
                stats.record_reconnect_attempt(self._client_id)
                stats.record_reconnect_success(self._client_id)
            else:
                stats.record_connection(self._client_id, True, connect_time)
                self._is_reconnect = True

            return True
        except Exception as e:
            if self._is_reconnect:
                stats.record_reconnect_attempt(self._client_id)
            else:
                stats.record_connection(self._client_id, False, 0.0)
            stats.record_error(f"Client {self._client_id} connect failed: {e}")
            await self._cleanup_ws()
            return False

    async def run(self, duration: float):
        self._running = True
        end_time = time.time() + duration

        self._send_task = asyncio.create_task(self._send_loop(end_time))
        self._heartbeat_task = asyncio.create_task(self._heartbeat_loop(end_time))

        try:
            while self._running and time.time() < end_time:
                if self._connected_event.is_set():
                    await self._recv_loop()
                    self._connected_event.clear()
                else:
                    await asyncio.sleep(self._current_backoff)
                    self._current_backoff = min(
                        self._current_backoff * 2, self.BACKOFF_MAX
                    )
                    success = await self.connect()
                    if success:
                        await self._drain_queue()
        except asyncio.CancelledError:
            pass
        finally:
            self._running = False
            self._cleanup_tasks()
            await self._cleanup()

    async def _send_loop(self, end_time: float):
        stats = StatisticsCollector()
        while self._running and time.time() < end_time:
            try:
                msg_id = str(uuid.uuid4())[:8]
                payload = self._generate_payload()
                msg_data = json.dumps({
                    "type": "message",
                    "room": self._room,
                    "client_id": self._client_id,
                    "msg_id": msg_id,
                    "content": payload,
                })
                send_ts = time.time()

                if (
                    self._connected_event.is_set()
                    and self._ws is not None
                    and not self._ws.closed
                ):
                    self._pending_messages[msg_id] = send_ts
                    try:
                        await self._ws.send_str(msg_data)
                        stats.record_message_sent()
                    except Exception:
                        self._enqueue_message(msg_data, msg_id, send_ts)
                else:
                    self._enqueue_message(msg_data, msg_id, send_ts)

                await asyncio.sleep(self._msg_interval)
            except asyncio.CancelledError:
                break
            except Exception as e:
                stats.record_error(f"Client {self._client_id} send error: {e}")
                await asyncio.sleep(0.5)

    def _enqueue_message(self, msg_data: str, msg_id: str, send_ts: float):
        try:
            self._message_queue.put_nowait((msg_data, msg_id, send_ts))
        except asyncio.QueueFull:
            try:
                self._message_queue.get_nowait()
            except asyncio.QueueEmpty:
                pass
            try:
                self._message_queue.put_nowait((msg_data, msg_id, send_ts))
            except asyncio.QueueFull:
                pass

    async def _drain_queue(self):
        stats = StatisticsCollector()
        drained = 0
        while not self._message_queue.empty() and drained < self.MAX_BUFFERED_MESSAGES:
            try:
                msg_data, msg_id, original_send_ts = self._message_queue.get_nowait()
                if self._ws is not None and not self._ws.closed:
                    send_ts = time.time()
                    self._pending_messages[msg_id] = send_ts
                    await self._ws.send_str(msg_data)
                    stats.record_message_sent()
                    drained += 1
                else:
                    break
            except asyncio.QueueEmpty:
                break
            except Exception:
                break

    async def _heartbeat_loop(self, end_time: float):
        while self._running and time.time() < end_time:
            if (
                self._connected_event.is_set()
                and self._ws is not None
                and not self._ws.closed
            ):
                try:
                    ping_msg = json.dumps({
                        "type": "ping",
                        "client_id": self._client_id,
                        "ts": time.time(),
                    })
                    await self._ws.send_str(ping_msg)
                except Exception:
                    pass
            await asyncio.sleep(self.HEARTBEAT_INTERVAL)

    async def _recv_loop(self):
        stats = StatisticsCollector()
        try:
            async for msg in self._ws:
                if msg.type == aiohttp.WSMsgType.TEXT:
                    self._last_recv_ts = time.time()
                    try:
                        data = json.loads(msg.data)
                        msg_id = data.get("msg_id")
                        if msg_id and msg_id in self._pending_messages:
                            recv_ts = time.time()
                            send_ts = self._pending_messages.pop(msg_id)
                            latency_ms = (recv_ts - send_ts) * 1000
                            stats.record_message_received(latency_ms)
                        elif data.get("type") == "broadcast":
                            broadcast_msg_id = data.get("msg_id")
                            if broadcast_msg_id and broadcast_msg_id in self._pending_messages:
                                recv_ts = time.time()
                                send_ts = self._pending_messages.pop(broadcast_msg_id)
                                latency_ms = (recv_ts - send_ts) * 1000
                                stats.record_message_received(latency_ms)
                    except (json.JSONDecodeError, KeyError):
                        pass
                elif msg.type == aiohttp.WSMsgType.PONG:
                    self._last_recv_ts = time.time()
                elif msg.type in (
                    aiohttp.WSMsgType.CLOSE,
                    aiohttp.WSMsgType.ERROR,
                    aiohttp.WSMsgType.CLOSED,
                ):
                    break
        except Exception as e:
            stats.record_error(f"Client {self._client_id} recv error: {e}")

    def _generate_payload(self) -> str:
        length = random.randint(self._msg_min, self._msg_max)
        return "".join(
            random.choices(string.ascii_letters + string.digits + " ", k=length)
        )

    def _cleanup_tasks(self):
        for task in [self._send_task, self._heartbeat_task]:
            if task is not None and not task.done():
                task.cancel()

    async def stop(self):
        self._running = False
        self._cleanup_tasks()
        await self._cleanup()

    async def _cleanup_ws(self):
        if self._ws is not None and not self._ws.closed:
            try:
                await self._ws.close()
            except Exception:
                pass

    async def _cleanup(self):
        await self._cleanup_ws()
        if self._session is not None and not self._session.closed:
            try:
                await self._session.close()
            except Exception:
                pass
