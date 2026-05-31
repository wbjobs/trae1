"""Background monitor that persists syscall events to PostgreSQL and
notifies the alert manager when a syscall exceeds the slow threshold."""
from __future__ import annotations

import threading
from datetime import datetime, timezone
from typing import Dict, Optional

from .alerts import get_alert_manager
from .database import SessionLocal
from .models import SyscallEvent as SyscallEventModel
from .monitor import SyscallEvent, SyscallMonitor


class MonitorManager:
    """Manages active monitors keyed by PID, storing events to PostgreSQL
    and pushing slow-syscall alerts through the AlertManager."""

    def __init__(self):
        self._monitors: Dict[int, SyscallMonitor] = {}
        self._lock = threading.Lock()

    def _persist(self, event: SyscallEvent) -> None:
        try:
            session = SessionLocal()
            try:
                ts = datetime.fromtimestamp(event.timestamp_ns / 1e9, tz=timezone.utc)
                extra = {
                    "arg1": event.arg1,
                    "arg2": event.arg2,
                    "arg3": event.arg3,
                }
                row = SyscallEventModel(
                    pid=event.pid,
                    comm=event.comm,
                    syscall_name=event.syscall_name,
                    timestamp=ts,
                    duration_ns=event.duration_ns,
                    return_value=event.return_value,
                    file_path=event.file_path,
                    extra_args=str(extra),
                )
                session.add(row)
                session.commit()
            finally:
                session.close()
        except Exception:
            pass
        # Alert manager (separate try/except: never let alerts break persistence)
        try:
            get_alert_manager().handle_event(event)
        except Exception:
            pass

    def start(self, pid: int) -> bool:
        with self._lock:
            if pid in self._monitors and self._monitors[pid].is_running():
                return False
            mon = SyscallMonitor(pid=pid, handler=self._persist)
            mon.start()
            mon.run_forever_async()
            self._monitors[pid] = mon
            return True

    def stop(self, pid: int) -> bool:
        with self._lock:
            mon = self._monitors.pop(pid, None)
        if mon is None:
            return False
        try:
            mon.stop()
        except Exception:
            pass
        return True

    def status(self) -> Dict[int, str]:
        with self._lock:
            return {
                pid: ("running" if m.is_running() else "stopped")
                for pid, m in self._monitors.items()
            }

    def is_running(self, pid: int) -> bool:
        with self._lock:
            m = self._monitors.get(pid)
            return m is not None and m.is_running()

    def stop_all(self) -> None:
        with self._lock:
            pids = list(self._monitors.keys())
        for pid in pids:
            self.stop(pid)


_manager: Optional[MonitorManager] = None


def get_monitor_manager() -> MonitorManager:
    global _manager
    if _manager is None:
        _manager = MonitorManager()
    return _manager
