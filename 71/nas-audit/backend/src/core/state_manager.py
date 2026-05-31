import json
import os
import time
import logging
import hashlib
import threading
from typing import Dict, List, Optional, Tuple
from collections import OrderedDict

logger = logging.getLogger(__name__)


class StateManager:
    def __init__(self, state_file: str = "data/audit_state.json",
                 max_processed_events: int = 10000):
        self.state_file = state_file
        self.max_processed_events = max_processed_events
        self._lock = threading.Lock()
        self._state = self._load_state()

    def _load_state(self) -> dict:
        default = {
            "last_snapshot": {},
            "processed_events": [],
            "last_checkpoint": 0.0,
            "reconnect_count": 0,
        }
        if not os.path.exists(self.state_file):
            logger.info(f"No state file found at {self.state_file}, starting fresh")
            return default

        try:
            with open(self.state_file, "r", encoding="utf-8") as f:
                data = json.load(f)
            logger.info(
                f"Loaded state: {len(data.get('last_snapshot', {}))} files, "
                f"{len(data.get('processed_events', []))} processed events, "
                f"last checkpoint {data.get('last_checkpoint', 0)}"
            )
            return data
        except (json.JSONDecodeError, IOError) as e:
            logger.warning(f"Failed to load state file: {e}, starting fresh")
            return default

    def save_state(self):
        os.makedirs(os.path.dirname(self.state_file) or ".", exist_ok=True)
        with self._lock:
            try:
                with open(self.state_file, "w", encoding="utf-8") as f:
                    json.dump(self._state, f, indent=2, ensure_ascii=False)
                logger.debug("State saved")
            except IOError as e:
                logger.error(f"Failed to save state: {e}")

    def get_last_snapshot(self) -> Dict[str, dict]:
        with self._lock:
            return dict(self._state.get("last_snapshot", {}))

    def set_last_snapshot(self, snapshot: Dict[str, dict]):
        with self._lock:
            self._state["last_snapshot"] = snapshot
            self._state["last_checkpoint"] = time.time()
            self._trim_processed_events()

    def _trim_processed_events(self):
        events = self._state.get("processed_events", [])
        if len(events) > self.max_processed_events:
            self._state["processed_events"] = events[-self.max_processed_events:]

    def _event_hash(self, file_path: str, operation_type: str, timestamp: float) -> str:
        key = f"{file_path}|{operation_type}|{timestamp:.3f}"
        return hashlib.md5(key.encode("utf-8")).hexdigest()

    def is_event_processed(self, file_path: str, operation_type: str,
                           timestamp: float, window_ms: int = 200) -> bool:
        with self._lock:
            target_hash = self._event_hash(file_path, operation_type, timestamp)
            target_window = timestamp - (window_ms / 1000.0)

            for entry in reversed(self._state.get("processed_events", [])):
                if entry.get("ts", 0) < target_window:
                    break
                if entry.get("hash") == target_hash:
                    return True

                stored_path = entry.get("path", "")
                stored_op = entry.get("op", "")
                stored_ts = entry.get("ts", 0)

                if (stored_path == file_path
                        and stored_op == operation_type
                        and abs(stored_ts - timestamp) < (window_ms / 1000.0)):
                    return True

            return False

    def mark_event_processed(self, file_path: str, operation_type: str, timestamp: float):
        with self._lock:
            hash_val = self._event_hash(file_path, operation_type, timestamp)
            self._state.setdefault("processed_events", []).append({
                "hash": hash_val,
                "path": file_path,
                "op": operation_type,
                "ts": timestamp,
            })
            self._trim_processed_events()

    def get_last_checkpoint(self) -> float:
        with self._lock:
            return self._state.get("last_checkpoint", 0.0)

    def increment_reconnect_count(self):
        with self._lock:
            self._state["reconnect_count"] = self._state.get("reconnect_count", 0) + 1
            return self._state["reconnect_count"]

    def get_reconnect_count(self) -> int:
        with self._lock:
            return self._state.get("reconnect_count", 0)
