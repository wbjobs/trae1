import os
import time
import threading
import logging
from typing import Callable, Optional, Set
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler, FileSystemEvent

logger = logging.getLogger(__name__)


class RuleFileHandler(FileSystemEventHandler):
    VALID_EXTENSIONS = {".yar", ".yara"}

    def __init__(
        self,
        rules_dir: str,
        on_change_callback: Callable[[Set[str]], None],
        debounce_seconds: float = 2.0,
    ):
        super().__init__()
        self.rules_dir = os.path.abspath(rules_dir)
        self.on_change_callback = on_change_callback
        self.debounce_seconds = debounce_seconds
        self._changed_files: Set[str] = set()
        self._debounce_timer: Optional[threading.Timer] = None
        self._lock = threading.Lock()

    def _is_valid_rule_file(self, filepath: str) -> bool:
        if not filepath:
            return False
        ext = os.path.splitext(filepath)[1].lower()
        return ext in self.VALID_EXTENSIONS

    def _schedule_debounce(self, filepath: str):
        if not self._is_valid_rule_file(filepath):
            return

        with self._lock:
            self._changed_files.add(os.path.abspath(filepath))

            if self._debounce_timer is not None:
                self._debounce_timer.cancel()

            self._debounce_timer = threading.Timer(
                self.debounce_seconds,
                self._fire_callback,
            )
            self._debounce_timer.daemon = True
            self._debounce_timer.start()

    def _fire_callback(self):
        with self._lock:
            changed = self._changed_files.copy()
            self._changed_files.clear()
            self._debounce_timer = None

        if changed:
            logger.info(f"Detected changes in rule files: {changed}")
            try:
                self.on_change_callback(changed)
            except Exception as e:
                logger.error(f"Error in rule change callback: {e}")

    def on_created(self, event: FileSystemEvent):
        if not event.is_directory:
            logger.debug(f"File created: {event.src_path}")
            self._schedule_debounce(event.src_path)

    def on_modified(self, event: FileSystemEvent):
        if not event.is_directory:
            logger.debug(f"File modified: {event.src_path}")
            self._schedule_debounce(event.src_path)

    def on_deleted(self, event: FileSystemEvent):
        if not event.is_directory:
            logger.debug(f"File deleted: {event.src_path}")
            self._schedule_debounce(event.src_path)

    def on_moved(self, event: FileSystemEvent):
        if not event.is_directory:
            logger.debug(f"File moved: {event.src_path} -> {event.dest_path}")
            self._schedule_debounce(event.src_path)
            self._schedule_debounce(event.dest_path)


class RuleFileWatcher:
    def __init__(
        self,
        rules_dir: str,
        on_change_callback: Callable[[Set[str]], None],
        debounce_seconds: float = 2.0,
    ):
        self.rules_dir = os.path.abspath(rules_dir)
        self.on_change_callback = on_change_callback
        self.debounce_seconds = debounce_seconds
        self._observer: Optional[Observer] = None
        self._handler: Optional[RuleFileHandler] = None
        self._running = False

    def start(self) -> bool:
        if self._running:
            logger.warning("Rule file watcher is already running")
            return False

        if not os.path.isdir(self.rules_dir):
            logger.error(f"Rules directory does not exist: {self.rules_dir}")
            return False

        try:
            self._handler = RuleFileHandler(
                rules_dir=self.rules_dir,
                on_change_callback=self.on_change_callback,
                debounce_seconds=self.debounce_seconds,
            )

            self._observer = Observer()
            self._observer.schedule(
                self._handler,
                self.rules_dir,
                recursive=False,
            )
            self._observer.daemon = True
            self._observer.start()
            self._running = True
            logger.info(f"Rule file watcher started for: {self.rules_dir}")
            return True

        except Exception as e:
            logger.error(f"Failed to start rule file watcher: {e}")
            self._cleanup()
            return False

    def stop(self):
        if not self._running:
            return

        logger.info("Stopping rule file watcher...")
        self._cleanup()
        self._running = False
        logger.info("Rule file watcher stopped")

    def _cleanup(self):
        if self._observer is not None:
            try:
                self._observer.stop()
                self._observer.join(timeout=5)
            except Exception as e:
                logger.error(f"Error stopping observer: {e}")
            finally:
                self._observer = None

        self._handler = None

    @property
    def is_running(self) -> bool:
        return self._running

    def get_status(self) -> dict:
        return {
            "running": self._running,
            "rules_dir": self.rules_dir,
            "debounce_seconds": self.debounce_seconds,
        }
