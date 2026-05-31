import logging
import time
import struct
import threading
import uuid
import random
from collections import defaultdict
from typing import Dict, List, Optional, Set, Tuple
from pathlib import Path

from smb.SMBConnection import SMBConnection

from .config import SMBConfig
from .event_models import FileOperationEvent, OperationType
from .state_manager import StateManager

logger = logging.getLogger(__name__)

FILE_NOTIFY_CHANGE_FILE_NAME = 0x00000001
FILE_NOTIFY_CHANGE_DIR_NAME = 0x00000002
FILE_NOTIFY_CHANGE_ATTRIBUTES = 0x00000004
FILE_NOTIFY_CHANGE_SIZE = 0x00000008
FILE_NOTIFY_CHANGE_LAST_WRITE = 0x00000010
FILE_NOTIFY_CHANGE_LAST_ACCESS = 0x00000020
FILE_NOTIFY_CHANGE_CREATION = 0x00000040
FILE_NOTIFY_CHANGE_EA = 0x00000080
FILE_NOTIFY_CHANGE_SECURITY = 0x00000100
FILE_NOTIFY_CHANGE_STREAM_NAME = 0x00000200
FILE_NOTIFY_CHANGE_STREAM_SIZE = 0x00000400
FILE_NOTIFY_CHANGE_STREAM_WRITE = 0x00000800

NOTIFY_COMPLETION_FILTER = (
    FILE_NOTIFY_CHANGE_FILE_NAME
    | FILE_NOTIFY_CHANGE_SIZE
    | FILE_NOTIFY_CHANGE_LAST_WRITE
    | FILE_NOTIFY_CHANGE_CREATION
)

ACTION_ADDED = 1
ACTION_REMOVED = 2
ACTION_MODIFIED = 3
ACTION_RENAMED_OLD = 4
ACTION_RENAMED_NEW = 5

ACTION_MAP = {
    ACTION_ADDED: OperationType.CREATE,
    ACTION_REMOVED: OperationType.DELETE,
    ACTION_MODIFIED: OperationType.MODIFY,
    ACTION_RENAMED_OLD: OperationType.RENAME,
    ACTION_RENAMED_NEW: OperationType.RENAME,
}


class SMBFileMonitor:
    def __init__(self, config: SMBConfig, state_manager: StateManager):
        self.config = config
        self.state_manager = state_manager

        self._pysmb_conn: Optional[SMBConnection] = None
        self._smb_conn = None
        self._smb_session = None
        self._smb_tree = None

        self._running = False
        self._connected = False
        self._lock = threading.RLock()
        self._event_callbacks: List = []

        self._ext_filter: Set[str] = set(
            ext.lower() for ext in config.monitored_extensions
        )

        self._notify_threads: Dict[str, threading.Thread] = {}
        self._notify_handles: Dict[str, object] = {}

        self._reconnect_thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()

        self._dedup_window_ms = getattr(config, 'dedup_window_ms', 200)
        self._batch_flush_interval = 0.5
        self._event_buffer: List[FileOperationEvent] = []
        self._buffer_lock = threading.Lock()
        self._buffer_flush_thread: Optional[threading.Thread] = None

        self._rename_pending: Dict[str, FileOperationEvent] = {}
        self._rename_lock = threading.Lock()

    def register_callback(self, callback):
        self._event_callbacks.append(callback)

    def _matches_extension_filter(self, file_path: str) -> bool:
        if not self._ext_filter:
            return True
        ext = Path(file_path).suffix.lower()
        return ext in self._ext_filter

    def connect(self) -> bool:
        with self._lock:
            try:
                self._pysmb_conn = SMBConnection(
                    self.config.username,
                    self.config.password,
                    "NAS_AUDIT_CLIENT",
                    self.config.server,
                    domain=self.config.domain,
                    use_ntlm_v2=True,
                )
                result = self._pysmb_conn.connect(
                    self.config.server, self.config.port
                )
                if not result:
                    logger.error("pysmb connection failed")
                    return False

                self._connected = True
                logger.info(
                    f"SMB connected to {self.config.server}:{self.config.port} "
                    f"(share: {self.config.share_name})"
                )
                return True
            except Exception as e:
                logger.error(f"SMB connection error: {e}")
                self._connected = False
                return False

    def _connect_smbprotocol(self) -> bool:
        try:
            import smbprotocol
            from smbprotocol.connection import Connection as SMConnection
            from smbprotocol.session import Session as SMSession
            from smbprotocol.tree import TreeConnect as SMTreeConnect

            port = self.config.port
            self._smb_conn = SMConnection(
                uuid.uuid4(),
                self.config.server,
                port=port,
            )
            self._smb_conn.connect(timeout=30)
            self._smb_session = SMSession(self._smb_conn)
            self._smb_session.connect(
                self.config.username,
                self.config.password,
                self.config.domain,
            )
            self._smb_tree = SMTreeConnect(
                self._smb_session, self.config.share_name
            )
            self._smb_tree.connect()
            logger.info("smbprotocol connection established for Change Notify")
            return True
        except ImportError as e:
            logger.error(f"smbprotocol not available: {e}")
            return False
        except Exception as e:
            logger.error(f"smbprotocol connection error: {e}")
            return False

    def _open_directory_for_notify(self, path: str):
        try:
            from smbprotocol.open import Open, CreateDisposition, FileAttributes
            from smbprotocol.structure import AccessMask, ShareAccess, CreateOptions

            smb_path = path.lstrip("/").replace("/", "\\")
            if smb_path == "":
                smb_path = "\\"

            dir_handle = Open(self._smb_tree, smb_path)
            dir_handle.open(
                CreateDisposition.FILE_OPEN,
                access_mask=(
                    AccessMask.FILE_LIST_DIRECTORY
                    | AccessMask.SYNCHRONIZE
                    | AccessMask.FILE_READ_ATTRIBUTES
                ),
                file_attributes=FileAttributes.FILE_ATTRIBUTE_DIRECTORY,
                share_access=(
                    ShareAccess.FILE_SHARE_READ
                    | ShareAccess.FILE_SHARE_WRITE
                    | ShareAccess.FILE_SHARE_DELETE
                ),
                create_options=CreateOptions.FILE_DIRECTORY_FILE,
            )
            return dir_handle
        except Exception as e:
            logger.error(f"Failed to open directory for notify '{path}': {e}")
            return None

    def disconnect(self):
        with self._lock:
            self._connected = False
            for handle in self._notify_handles.values():
                try:
                    handle.close()
                except Exception:
                    pass
            self._notify_handles.clear()

            if self._smb_tree:
                try:
                    self._smb_tree.disconnect()
                except Exception:
                    pass
                self._smb_tree = None

            if self._smb_session:
                try:
                    self._smb_session.disconnect()
                except Exception:
                    pass
                self._smb_session = None

            if self._smb_conn:
                try:
                    self._smb_conn.disconnect()
                except Exception:
                    pass
                self._smb_conn = None

            if self._pysmb_conn:
                try:
                    self._pysmb_conn.close()
                except Exception:
                    pass
                self._pysmb_conn = None

            self._notify_threads.clear()
            logger.info("SMB disconnected")

    def _list_files(self, path: str) -> Dict[str, Dict]:
        files = {}
        if not self._pysmb_conn:
            return files

        try:
            entries = self._pysmb_conn.listPath(
                self.config.share_name, path
            )
            for entry in entries:
                if entry.filename in (".", ".."):
                    continue

                full_path = (
                    entry.filename if path == "/" else f"{path}/{entry.filename}"
                )

                if entry.isDirectory:
                    sub_files = self._list_files(full_path)
                    files.update(sub_files)
                else:
                    ext = Path(entry.filename).suffix.lower()
                    files[full_path] = {
                        "name": entry.filename,
                        "size": entry.file_size,
                        "last_write": entry.last_write_time,
                        "is_dir": False,
                        "extension": ext,
                    }
        except Exception as e:
            logger.warning(f"Error listing path {path}: {e}")

        return files

    def _take_snapshot(self) -> Dict[str, Dict]:
        snapshot = {}
        for watch_path in self.config.watch_paths:
            files = self._list_files(watch_path)
            snapshot.update(files)
        return snapshot

    def _detect_changes(
        self, current: Dict[str, Dict], previous: Dict[str, Dict]
    ) -> List[FileOperationEvent]:
        events = []
        now = time.time()

        current_paths = set(current.keys())
        previous_paths = set(previous.keys())

        created = current_paths - previous_paths
        deleted = previous_paths - current_paths

        for path in created:
            if not self._matches_extension_filter(path):
                continue
            info = current[path]
            events.append(
                FileOperationEvent(
                    operation_type=OperationType.CREATE,
                    file_path=path,
                    timestamp=now,
                    file_size=info["size"],
                    file_extension=info["extension"],
                )
            )

        for path in deleted:
            if not self._matches_extension_filter(path):
                continue
            info = previous[path]
            events.append(
                FileOperationEvent(
                    operation_type=OperationType.DELETE,
                    file_path=path,
                    timestamp=now,
                    file_size=info["size"],
                    file_extension=info["extension"],
                )
            )

        common = current_paths & previous_paths
        for path in common:
            prev_info = previous[path]
            curr_info = current[path]

            if not self._matches_extension_filter(path):
                continue

            if (
                prev_info["size"] != curr_info["size"]
                or prev_info["last_write"] != curr_info["last_write"]
            ):
                events.append(
                    FileOperationEvent(
                        operation_type=OperationType.MODIFY,
                        file_path=path,
                        timestamp=now,
                        file_size=curr_info["size"],
                        file_extension=curr_info["extension"],
                    )
                )

        return events

    def _detect_renames(
        self,
        created_events: List[FileOperationEvent],
        deleted_events: List[FileOperationEvent],
        current: Dict[str, Dict],
        previous: Dict[str, Dict],
    ) -> List[FileOperationEvent]:
        if not created_events or not deleted_events:
            return created_events + deleted_events

        rename_events = []
        used_created = set()
        used_deleted = set()

        for del_evt in deleted_events:
            del_info = previous.get(del_evt.file_path, {})
            del_size = del_info.get("size", 0)

            for i, cre_evt in enumerate(created_events):
                if i in used_created:
                    continue
                cre_info = current.get(cre_evt.file_path, {})
                cre_size = cre_info.get("size", 0)

                if (
                    abs(del_size - cre_size) < 1024
                    and cre_evt.file_extension == del_evt.file_extension
                ):
                    rename_events.append(
                        FileOperationEvent(
                            operation_type=OperationType.RENAME,
                            file_path=cre_evt.file_path,
                            old_file_path=del_evt.file_path,
                            timestamp=cre_evt.timestamp,
                            file_size=cre_size,
                            file_extension=cre_evt.file_extension,
                        )
                    )
                    used_created.add(i)
                    used_deleted.add(deleted_events.index(del_evt))
                    break

        filtered_created = [
            e for i, e in enumerate(created_events) if i not in used_created
        ]
        filtered_deleted = [
            e for i, e in enumerate(deleted_events) if i not in used_deleted
        ]

        return filtered_created + filtered_deleted + rename_events

    def _parse_notify_response(
        self, response, base_path: str
    ) -> List[FileOperationEvent]:
        events = []
        now = time.time()

        if response is None:
            return events

        try:
            output_buffer = None
            if hasattr(response, "get_value"):
                output_buffer = response.get_value()
            elif hasattr(response, "__getitem__"):
                if "output_buffer" in response:
                    ob = response["output_buffer"]
                    output_buffer = ob.get_value() if hasattr(ob, "get_value") else bytes(ob)
                elif "buffer" in response:
                    buf = response["buffer"]
                    output_buffer = buf.get_value() if hasattr(buf, "get_value") else bytes(buf)

            if not output_buffer or len(output_buffer) < 12:
                return events

            offset = 0
            buffer_len = len(output_buffer)

            while offset < buffer_len:
                if offset + 12 > buffer_len:
                    break

                next_entry_offset = struct.unpack_from("<I", output_buffer, offset)[0]
                action = struct.unpack_from("<I", output_buffer, offset + 4)[0]
                file_name_length = struct.unpack_from("<I", output_buffer, offset + 8)[0]

                if offset + 12 + file_name_length > buffer_len:
                    break

                file_name_bytes = output_buffer[
                    offset + 12 : offset + 12 + file_name_length
                ]
                try:
                    file_name = file_name_bytes.decode("utf-16-le", errors="replace")
                except Exception:
                    file_name = file_name_bytes.decode("latin-1", errors="replace")

                if not file_name or file_name in (".", ".."):
                    if next_entry_offset == 0:
                        break
                    offset += next_entry_offset
                    continue

                full_path = (
                    file_name
                    if base_path == "/"
                    else f"{base_path}/{file_name}"
                )

                if not self._matches_extension_filter(full_path):
                    if next_entry_offset == 0:
                        break
                    offset += next_entry_offset
                    continue

                op_type = ACTION_MAP.get(action, OperationType.ACCESS)
                ext = Path(file_name).suffix.lower()

                event = FileOperationEvent(
                    operation_type=op_type,
                    file_path=full_path,
                    timestamp=now,
                    file_extension=ext,
                )

                if action == ACTION_RENAMED_NEW:
                    with self._rename_lock:
                        for key, pending in list(self._rename_pending.items()):
                            if (
                                pending.operation_type == OperationType.RENAME
                                and pending.old_file_path is None
                            ):
                                event.old_file_path = pending.file_path
                                del self._rename_pending[key]
                                break
                elif action == ACTION_RENAMED_OLD:
                    with self._rename_lock:
                        self._rename_pending[full_path] = event
                        if next_entry_offset == 0:
                            break
                        offset += next_entry_offset
                        continue

                events.append(event)

                if next_entry_offset == 0:
                    break
                offset += next_entry_offset

        except Exception as e:
            logger.error(f"Error parsing notify response: {e}")

        return events

    def _is_duplicate(self, event: FileOperationEvent) -> bool:
        return self.state_manager.is_event_processed(
            event.file_path,
            event.operation_type.value,
            event.timestamp,
            self._dedup_window_ms,
        )

    def _emit_event(self, event: FileOperationEvent):
        if self._is_duplicate(event):
            logger.debug(f"Duplicate event suppressed: {event.operation_type.value} {event.file_path}")
            return

        with self._buffer_lock:
            self._event_buffer.append(event)

    def _flush_buffer(self):
        with self._buffer_lock:
            if not self._event_buffer:
                return
            batch = self._event_buffer
            self._event_buffer = []

        for event in batch:
            for callback in self._event_callbacks:
                try:
                    callback(event)
                except Exception as e:
                    logger.error(f"Callback error: {e}")
            self.state_manager.mark_event_processed(
                event.file_path,
                event.operation_type.value,
                event.timestamp,
            )

        logger.debug(f"Flushed batch of {len(batch)} events")

    def _buffer_flush_loop(self):
        while not self._stop_event.is_set():
            self._flush_buffer()
            self._stop_event.wait(self._batch_flush_interval)
        self._flush_buffer()

    def _notify_loop(self, path: str):
        logger.info(f"Notify loop started for path: {path}")
        retry_count = 0

        while not self._stop_event.is_set() and self._running:
            if not self._connected:
                self._stop_event.wait(2)
                continue

            try:
                dir_handle = self._open_directory_for_notify(path)
                if not dir_handle:
                    self._stop_event.wait(5)
                    continue

                self._notify_handles[path] = dir_handle
                retry_count = 0

                while not self._stop_event.is_set() and self._connected:
                    try:
                        response = self._smb_session.notify(
                            tree=self._smb_tree.tree_id,
                            file_id=dir_handle.file_id,
                            completion_filter=NOTIFY_COMPLETION_FILTER,
                            recursive=True,
                            change_notify_timeout=300,
                        )

                        events = self._parse_notify_response(response, path)
                        for event in events:
                            self._emit_event(event)

                    except Exception as e:
                        err_str = str(e).lower()
                        if any(
                            kw in err_str
                            for kw in ["timeout", "timed out", "cancelled", "invalid"]
                        ):
                            logger.debug(f"Notify loop timeout for '{path}': {e}")
                            continue
                        elif any(
                            kw in err_str
                            for kw in ["disconnected", "network", "reset", "broken pipe"]
                        ):
                            logger.warning(f"Connection lost in notify loop for '{path}': {e}")
                            self._connected = False
                            break
                        else:
                            logger.warning(f"Notify error for '{path}': {e}")
                            time.sleep(0.5)
                            continue

            except Exception as e:
                logger.warning(f"Notify loop error for '{path}': {e}")
                retry_count += 1
                if self._stop_event.is_set():
                    break

                backoff = min(30, 2 ** min(retry_count, 5) + random.uniform(0, 1))
                self._stop_event.wait(backoff)

            finally:
                if path in self._notify_handles:
                    try:
                        self._notify_handles[path].close()
                    except Exception:
                        pass
                    del self._notify_handles[path]

        logger.info(f"Notify loop stopped for path: {path}")

    def _reconnect_loop(self):
        logger.info("Reconnection monitor started")

        while not self._stop_event.is_set():
            if self._connected:
                self._stop_event.wait(10)
                continue

            logger.warning("Connection lost, initiating reconnection...")

            self._reconnect_with_catchup()

            self._stop_event.wait(1)

        logger.info("Reconnection monitor stopped")

    def _reconnect_with_catchup(self):
        max_retries = 10
        retry_count = 0

        while not self._stop_event.is_set() and retry_count < max_retries:
            retry_count += 1
            self.state_manager.increment_reconnect_count()

            logger.info(f"Reconnection attempt {retry_count}/{max_retries}")

            self.disconnect()

            if not self.connect():
                backoff = min(60, 2 ** min(retry_count, 6) + random.uniform(0, 2))
                logger.info(f"Reconnection failed, waiting {backoff:.1f}s...")
                self._stop_event.wait(backoff)
                continue

            if not self._connect_smbprotocol():
                backoff = min(60, 2 ** min(retry_count, 6) + random.uniform(0, 2))
                logger.info(f"smbprotocol connection failed, waiting {backoff:.1f}s...")
                self._stop_event.wait(backoff)
                continue

            logger.info("Reconnected successfully, performing catch-up...")

            last_snapshot = self.state_manager.get_last_snapshot()

            try:
                current_snapshot = self._take_snapshot()
            except Exception as e:
                logger.error(f"Failed to take snapshot during catch-up: {e}")
                current_snapshot = {}

            if last_snapshot and current_snapshot:
                missed_events = self._detect_changes(current_snapshot, last_snapshot)

                created_events = [
                    e for e in missed_events if e.operation_type == OperationType.CREATE
                ]
                deleted_events = [
                    e for e in missed_events if e.operation_type == OperationType.DELETE
                ]
                other_events = [
                    e
                    for e in missed_events
                    if e.operation_type
                    not in (OperationType.CREATE, OperationType.DELETE)
                ]

                resolved_events = self._detect_renames(
                    created_events, deleted_events, current_snapshot, last_snapshot
                )
                resolved_events.extend(other_events)

                logger.info(
                    f"Catch-up: {len(resolved_events)} missed events detected "
                    f"(created={len(created_events)}, deleted={len(deleted_events)})"
                )

                for event in resolved_events:
                    self._emit_event(event)

            self.state_manager.set_last_snapshot(current_snapshot)
            self.state_manager.save_state()

            for watch_path in self.config.watch_paths:
                if (
                    watch_path not in self._notify_threads
                    or not self._notify_threads[watch_path].is_alive()
                ):
                    t = threading.Thread(
                        target=self._notify_loop,
                        args=(watch_path,),
                        daemon=True,
                        name=f"notify-{watch_path}",
                    )
                    t.start()
                    self._notify_threads[watch_path] = t

            self._connected = True
            logger.info("Reconnection and catch-up complete")
            return

        logger.error("Max reconnection retries exceeded")

    def start(self):
        if self._running:
            return

        self._running = True
        self._stop_event.clear()

        if not self.connect():
            logger.error("Failed to connect, will retry in background")

        self._connect_smbprotocol()

        last_snapshot = self.state_manager.get_last_snapshot()
        try:
            current_snapshot = self._take_snapshot()
        except Exception as e:
            logger.error(f"Failed to take initial snapshot: {e}")
            current_snapshot = {}

        if last_snapshot and current_snapshot:
            initial_events = self._detect_changes(
                current_snapshot, last_snapshot
            )
            logger.info(
                f"Startup: {len(initial_events)} changes detected since last checkpoint"
            )
            for event in initial_events:
                self._emit_event(event)

        self.state_manager.set_last_snapshot(current_snapshot)
        self.state_manager.save_state()

        self._buffer_flush_thread = threading.Thread(
            target=self._buffer_flush_loop, daemon=True, name="buffer-flush"
        )
        self._buffer_flush_thread.start()

        for watch_path in self.config.watch_paths:
            t = threading.Thread(
                target=self._notify_loop,
                args=(watch_path,),
                daemon=True,
                name=f"notify-{watch_path}",
            )
            t.start()
            self._notify_threads[watch_path] = t

        self._reconnect_thread = threading.Thread(
            target=self._reconnect_loop, daemon=True, name="reconnect-monitor"
        )
        self._reconnect_thread.start()

        logger.info("SMB monitor started with Change Notify")

    def start_async(self):
        self.start()

    def stop(self):
        logger.info("Stopping SMB monitor...")
        self._running = False
        self._stop_event.set()

        current_snapshot = {}
        try:
            current_snapshot = self._take_snapshot()
        except Exception:
            pass

        if current_snapshot:
            self.state_manager.set_last_snapshot(current_snapshot)
        self.state_manager.save_state()

        for t in self._notify_threads.values():
            t.join(timeout=5)
        self._notify_threads.clear()

        if self._reconnect_thread:
            self._reconnect_thread.join(timeout=5)
            self._reconnect_thread = None

        if self._buffer_flush_thread:
            self._buffer_flush_thread.join(timeout=3)
            self._buffer_flush_thread = None

        self._flush_buffer()
        self.disconnect()
        logger.info("SMB monitor stopped")
