import logging
import time
import threading
import queue
from typing import Optional, List
from concurrent.futures import ThreadPoolExecutor

from .dlp_config import DLPConfig, load_dlp_config
from .tika_extractor import TikaExtractor
from .sensitive_matcher import SensitiveMatcher, MatchResult
from .alert_manager import AlertManager
from .quarantine_manager import QuarantineManager
from .event_models import FileOperationEvent, OperationType

logger = logging.getLogger(__name__)


class DLPPipeline:
    def __init__(self, dlp_cfg: DLPConfig, es_store, smb_monitor):
        self.config = dlp_cfg
        self.es_store = es_store
        self.smb_monitor = smb_monitor

        self.extractor = TikaExtractor(dlp_cfg.sensitive)
        self.matcher = SensitiveMatcher(dlp_cfg.sensitive)
        self.alert_mgr = AlertManager(dlp_cfg.alert, dlp_cfg.sensitive)
        self.quarantine_mgr = QuarantineManager(dlp_cfg.quarantine)

        self._scan_queue: "queue.Queue[FileOperationEvent]" = queue.Queue()
        self._executor = ThreadPoolExecutor(
            max_workers=dlp_cfg.sensitive.scan_concurrency,
            thread_name_prefix="dlp-scanner",
        )
        self._running = False
        self._lock = threading.Lock()

    def start(self):
        with self._lock:
            if self._running:
                return
            self._running = True
            for _ in range(self.config.sensitive.scan_concurrency):
                self._executor.submit(self._scan_worker)
            logger.info(
                f"DLP Pipeline started with {self.config.sensitive.scan_concurrency} workers"
            )

    def stop(self):
        with self._lock:
            if not self._running:
                return
            self._running = False
        self._executor.shutdown(wait=True)
        logger.info("DLP Pipeline stopped")

    def submit(self, event: FileOperationEvent):
        if not self.config.sensitive.enabled:
            return
        if event.operation_type not in (OperationType.CREATE, OperationType.MODIFY):
            return
        self._scan_queue.put(event)

    def _scan_worker(self):
        while self._running:
            try:
                event = self._scan_queue.get(timeout=1.0)
            except queue.Empty:
                continue

            try:
                self._process_event(event)
            except Exception as e:
                logger.error(f"DLP scan worker error: {e}", exc_info=True)

    def _process_event(self, event: FileOperationEvent):
        from pathlib import Path

        ext = Path(event.file_path).suffix.lower()
        if ext not in self.config.sensitive.scan_extensions:
            return

        if event.file_size > self.config.sensitive.max_file_size_mb * 1024 * 1024:
            return

        text, _ = self.extractor.extract_from_smb_path(self.smb_monitor, event.file_path)
        if not text:
            return

        matches = self.matcher.match(text)
        if not matches:
            return

        logger.warning(
            f"Sensitive content detected in {event.file_path}: "
            f"{len(matches)} matches, words={[m.word for m in matches[:5]]}"
        )

        severities = sorted(
            set(m.severity for m in matches),
            key=lambda s: {"critical": 0, "high": 1, "medium": 2, "low": 3}.get(s, 4),
        )
        top_severity = severities[0] if severities else "high"
        top_match = max(matches, key=lambda m: m.confidence, default=matches[0])

        dlp_doc = {
            "event": event.to_dict(),
            "matches": [
                {
                    "word": m.word,
                    "category": m.category,
                    "severity": m.severity,
                    "description": m.description,
                    "snippet": m.snippet[:500],
                    "position": m.position,
                    "regex": m.regex,
                    "confidence": m.confidence,
                }
                for m in matches
            ],
            "top_severity": top_severity,
            "total_matches": len(matches),
            "matched_words": list(set(m.word for m in matches)),
            "categories": list(set(m.category for m in matches)),
            "isolation_id": None,
            "quarantined": False,
            "quarantine_path": None,
            "@timestamp": time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime()),
        }

        write_index = self.es_store._get_write_index().replace(
            self.es_store.config.index_prefix,
            f"{self.es_store.config.index_prefix}_dlp",
        )
        try:
            awaitable = self.es_store._client.index(
                index=write_index,
                document=dlp_doc,
                refresh=False,
            )
            import asyncio
            loop = asyncio.get_event_loop()
            if loop.is_running():
                future = asyncio.run_coroutine_threadsafe(awaitable, loop)
                future.result(timeout=10)
            else:
                loop.run_until_complete(awaitable)
        except Exception as e:
            logger.error(f"Failed to index DLP event: {e}")

        self.alert_mgr.send_alert(event, matches)

        if self.quarantine_mgr.should_auto_quarantine(top_severity):
            reason = f"Auto-quarantine: {top_severity} sensitive content detected"
            success, qpath, iso_id = self.quarantine_mgr.quarantine_file(
                self.smb_monitor,
                event.file_path,
                username=event.username,
                reason=reason,
            )
            if success:
                logger.warning(
                    f"Auto-quarantined: {event.file_path} -> {qpath} (id={iso_id})"
                )

    def reload_words(self, words):
        from .dlp_config import SensitiveWord
        parsed = []
        for w in words:
            parsed.append(SensitiveWord(
                word=w.get("word", ""),
                category=w.get("category", "default"),
                regex=w.get("regex", False),
                severity=w.get("severity", "high"),
                description=w.get("description", ""),
            ))
        self.matcher.reload(parsed)
        self.config.sensitive.words = parsed
        logger.info(f"Sensitive words reloaded: {len(parsed)} words")

    def get_current_words(self):
        return [
            {
                "word": w.word,
                "category": w.category,
                "regex": w.regex,
                "severity": w.severity,
                "description": w.description,
            }
            for w in self.config.sensitive.words
        ]

    def scan_content(self, text: str) -> List[dict]:
        matches = self.matcher.match(text)
        return [
            {
                "word": m.word,
                "category": m.category,
                "severity": m.severity,
                "description": m.description,
                "snippet": m.snippet[:200],
                "position": m.position,
                "regex": m.regex,
                "confidence": m.confidence,
            }
            for m in matches
        ]
