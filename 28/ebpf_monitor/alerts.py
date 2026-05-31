"""Alert manager: receives slow-syscall alerts, persists them to PostgreSQL,
broadcasts them over WebSocket, and keeps a rolling in-memory buffer."""
from __future__ import annotations

import asyncio
import json
import logging
import threading
from collections import deque
from datetime import datetime, timezone
from typing import Deque, List, Optional, Set

from fastapi import WebSocket, WebSocketDisconnect

from .config import settings
from .database import SessionLocal
from .models import Alert as AlertModel
from .monitor import SyscallEvent


logger = logging.getLogger("ebpf_monitor.alerts")


def _threshold_ns() -> int:
    return max(1, int(settings.slow_threshold_ms)) * 1_000_000


def format_alert_message(event: SyscallEvent, threshold_ns: int) -> str:
    dur_ms = event.duration_ns / 1_000_000
    return (
        f"SLOW syscall {event.syscall_name} took {dur_ms:.2f} ms "
        f"(threshold {threshold_ns / 1_000_000:.0f} ms) pid={event.pid} "
        f"comm={event.comm} file={event.file_path or '-'}"
    )


class AlertManager:
    """Central coordinator for slow-syscall alerts.

    * Receives alerts from eBPF monitor handlers (any thread).
    * Persists them to PostgreSQL asynchronously (best-effort).
    * Broadcasts JSON payloads to all connected WebSocket clients.
    * Keeps a rolling buffer of recent alerts for the REST API.
    """

    def __init__(self, rolling_buffer_size: int = 500):
        self.threshold_ns = _threshold_ns()
        self._lock = threading.Lock()
        self._ws_clients: Set[WebSocket] = set()
        self._recent: Deque[dict] = deque(maxlen=rolling_buffer_size)
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._count = 0

    # ------------------------------------------------------------------
    # Threshold
    # ------------------------------------------------------------------
    def is_slow(self, event: SyscallEvent) -> bool:
        return event.duration_ns >= self.threshold_ns

    # ------------------------------------------------------------------
    # Public API (called from any thread, including BCC poll thread)
    # ------------------------------------------------------------------
    def handle_event(self, event: SyscallEvent) -> None:
        if not self.is_slow(event):
            return
        self._count += 1
        payload = self._event_to_payload(event)
        with self._lock:
            self._recent.append(payload)
        # Persist + broadcast on a background thread to avoid blocking
        # the BPF perf-buffer poll loop.
        try:
            t = threading.Thread(
                target=self._persist_and_broadcast, args=(payload,), daemon=True
            )
            t.start()
        except Exception as exc:
            logger.warning("Failed to dispatch alert: %s", exc)

    def set_event_loop(self, loop: asyncio.AbstractEventLoop) -> None:
        self._loop = loop

    # ------------------------------------------------------------------
    # WebSocket management (async)
    # ------------------------------------------------------------------
    async def add_client(self, ws: WebSocket) -> None:
        with self._lock:
            self._ws_clients.add(ws)
        logger.info("WebSocket client connected (total=%d)", len(self._ws_clients))
        try:
            await ws.send_json(
                {
                    "type": "hello",
                    "threshold_ns": self.threshold_ns,
                    "threshold_ms": settings.slow_threshold_ms,
                }
            )
            # Send recent alerts on connect
            with self._lock:
                recent = list(self._recent)
            for payload in recent[-50:]:
                await ws.send_json(payload)
            # Wait for disconnect
            while True:
                data = await ws.receive()
                if data.type == "websocket.disconnect":
                    break
        except WebSocketDisconnect:
            pass
        finally:
            with self._lock:
                self._ws_clients.discard(ws)
            logger.info("WebSocket client disconnected (total=%d)", len(self._ws_clients))

    # ------------------------------------------------------------------
    # REST API helpers (sync)
    # ------------------------------------------------------------------
    def recent_alerts(self, limit: int = 100) -> List[dict]:
        with self._lock:
            items = list(self._recent)
        return items[-limit:]

    def count(self) -> int:
        return self._count

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------
    def _event_to_payload(self, event: SyscallEvent) -> dict:
        ts = datetime.fromtimestamp(event.timestamp_ns / 1e9, tz=timezone.utc)
        return {
            "type": "slow_syscall",
            "id": self._count,
            "pid": event.pid,
            "comm": event.comm,
            "syscall_name": event.syscall_name,
            "timestamp": ts.isoformat(),
            "duration_ns": event.duration_ns,
            "duration_ms": event.duration_ns / 1_000_000,
            "threshold_ns": self.threshold_ns,
            "threshold_ms": settings.slow_threshold_ms,
            "file_path": event.file_path,
            "return_value": event.return_value,
            "message": format_alert_message(event, self.threshold_ns),
        }

    def _persist_and_broadcast(self, payload: dict) -> None:
        # 1) Persist
        try:
            session = SessionLocal()
            try:
                row = AlertModel(
                    pid=payload["pid"],
                    comm=payload["comm"],
                    syscall_name=payload["syscall_name"],
                    timestamp=datetime.fromisoformat(payload["timestamp"]),
                    duration_ns=payload["duration_ns"],
                    threshold_ns=payload["threshold_ns"],
                    file_path=payload.get("file_path"),
                    message=payload["message"],
                )
                session.add(row)
                session.commit()
            finally:
                session.close()
        except Exception as exc:
            logger.warning("Failed to persist alert: %s", exc)

        # 2) Log
        logger.warning("%s", payload["message"])

        # 3) Broadcast to WebSocket clients (schedule onto event loop)
        try:
            loop = self._loop
            if loop is None or loop.is_closed():
                return
            asyncio.run_coroutine_threadsafe(self._broadcast(payload), loop)
        except Exception as exc:
            logger.warning("Failed to schedule broadcast: %s", exc)

    async def _broadcast(self, payload: dict) -> None:
        with self._lock:
            clients = list(self._ws_clients)
        dead: List[WebSocket] = []
        for ws in clients:
            try:
                await ws.send_json(payload)
            except Exception:
                dead.append(ws)
        if dead:
            with self._lock:
                for ws in dead:
                    self._ws_clients.discard(ws)


_alert_manager: Optional[AlertManager] = None


def get_alert_manager() -> AlertManager:
    global _alert_manager
    if _alert_manager is None:
        _alert_manager = AlertManager()
    return _alert_manager
