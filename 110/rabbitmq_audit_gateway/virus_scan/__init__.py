"""Virus scanning module for RabbitMQ Audit Gateway"""
import os
import re
import base64
import logging
import threading
import time
import hashlib
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, Tuple
from enum import Enum
from concurrent.futures import ThreadPoolExecutor, Future
from abc import ABC, abstractmethod

logger = logging.getLogger(__name__)


class ScanMode(Enum):
    SYNC = "sync"
    ASYNC = "async"


class ScanResult:
    def __init__(
        self,
        message_id: str,
        is_infected: bool,
        virus_name: Optional[str] = None,
        scan_duration_ms: float = 0,
        scanner: str = "",
        error: Optional[str] = None,
        timeout: bool = False
    ):
        self.message_id = message_id
        self.is_infected = is_infected
        self.virus_name = virus_name
        self.scan_duration_ms = scan_duration_ms
        self.scanner = scanner
        self.error = error
        self.timeout = timeout
        self.timestamp = time.time()

    def to_dict(self) -> Dict[str, Any]:
        return {
            "message_id": self.message_id,
            "is_infected": self.is_infected,
            "virus_name": self.virus_name,
            "scan_duration_ms": self.scan_duration_ms,
            "scanner": self.scanner,
            "error": self.error,
            "timeout": self.timeout,
            "timestamp": self.timestamp
        }


@dataclass
class QuarantineMessage:
    message_id: str
    virus_name: str
    original_exchange: str
    original_routing_key: str
    body: bytes
    headers: Dict[str, Any]
    quarantined_at: float = field(default_factory=time.time)


class VirusScannerInterface(ABC):
    @abstractmethod
    def scan(self, data: bytes) -> Tuple[bool, Optional[str]]:
        pass

    @abstractmethod
    def get_name(self) -> str:
        pass

    @abstractmethod
    def is_available(self) -> bool:
        pass

    @abstractmethod
    def update_database(self) -> bool:
        pass


class ClamAVScanner(VirusScannerInterface):
    def __init__(self, socket_path: str = "/var/run/clamav/clamd.sock", host: str = "localhost", port: int = 3310):
        self._socket_path = socket_path
        self._host = host
        self._port = port
        self._available = False
        self._check_availability()

    def _check_availability(self):
        try:
            import socket
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2)
            result = sock.connect_ex((self._host, self._port))
            if result == 0:
                self._available = True
            sock.close()
        except Exception as e:
            logger.debug(f"ClamAV not available: {e}")
            self._available = False

    def scan(self, data: bytes) -> Tuple[bool, Optional[str]]:
        if not self._available:
            return False, None

        try:
            import socket
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)
            sock.connect((self._host, self._port))

            cmd = f"zINSTREAM\n".encode()
            sock.send(cmd)

            size = len(data).to_bytes(4, 'big')
            sock.send(size)
            sock.send(data)
            sock.send(b'\x00\x00\x00\x00')

            response = b''
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk

            sock.close()

            response_str = response.decode('utf-8', errors='ignore')
            if 'FOUND' in response_str:
                virus_name = response_str.split('FOUND')[0].strip().split(':')[-1].strip()
                return True, virus_name
            elif 'OK' in response_str:
                return False, None
            else:
                return False, None

        except Exception as e:
            logger.error(f"ClamAV scan error: {e}")
            return False, str(e)

    def get_name(self) -> str:
        return "clamav"

    def is_available(self) -> bool:
        return self._available

    def update_database(self) -> bool:
        try:
            import subprocess
            result = subprocess.run(
                ["freshclam", "--quiet"],
                capture_output=True,
                timeout=60
            )
            if result.returncode == 0:
                logger.info("ClamAV database updated successfully")
                return True
            else:
                logger.error(f"ClamAV update failed: {result.stderr.decode()}")
                return False
        except Exception as e:
            logger.error(f"ClamAV update error: {e}")
            return False


class YARAScanner(VirusScannerInterface):
    def __init__(self, rules_dir: str = "yara_rules"):
        self._rules_dir = rules_dir
        self._rules = []
        self._available = False
        self._load_rules()

    def _load_rules(self):
        try:
            import yara
            self._rules = []

            if not os.path.exists(self._rules_dir):
                os.makedirs(self._rules_dir, exist_ok=True)

            for filename in os.listdir(self._rules_dir):
                if filename.endswith('.yar') or filename.endswith('.yara'):
                    filepath = os.path.join(self._rules_dir, filename)
                    try:
                        rule = yara.compile(filepath=filepath)
                        self._rules.append((filename, rule))
                    except Exception as e:
                        logger.warning(f"Failed to load YARA rule {filename}: {e}")

            self._available = len(self._rules) > 0
        except ImportError:
            logger.warning("YARA not installed, YARA scanner disabled")
            self._available = False
        except Exception as e:
            logger.error(f"Failed to load YARA rules: {e}")
            self._available = False

    def scan(self, data: bytes) -> Tuple[bool, Optional[str]]:
        if not self._available:
            return False, None

        try:
            for rule_name, rule in self._rules:
                matches = rule.match(data=data)
                if matches:
                    virus_names = [m.rule for m in matches]
                    return True, ', '.join(virus_names)
            return False, None
        except Exception as e:
            logger.error(f"YARA scan error: {e}")
            return False, str(e)

    def get_name(self) -> str:
        return "yara"

    def is_available(self) -> bool:
        return self._available

    def update_database(self) -> bool:
        try:
            if not os.path.exists(self._rules_dir):
                os.makedirs(self._rules_dir, exist_ok=True)

            logger.info("YARA rules updated - reload rules")
            self._load_rules()
            return True
        except Exception as e:
            logger.error(f"YARA rules update error: {e}")
            return False


class AttachmentExtractor:
    BASE64_PATTERN = re.compile(
        r'(?:data:([\w/.]+);base64,)?([A-Za-z0-9+/=\n\r]+)'
    )

    BASE64_MIN_LENGTH = 100

    @staticmethod
    def extract_base64_attachments(body: bytes) -> List[Tuple[str, bytes]]:
        """Extract base64 encoded attachments from message body"""
        attachments = []
        text = body.decode('utf-8', errors='ignore')

        matches = AttachmentExtractor.BASE64_PATTERN.finditer(text)
        for match in matches:
            mime_type = match.group(1) or "application/octet-stream"
            base64_data = match.group(2)

            base64_clean = base64_data.replace('\n', '').replace('\r', '').strip()

            if len(base64_clean) < AttachmentExtractor.BASE64_MIN_LENGTH:
                continue

            if len(base64_clean) % 4 != 0:
                padding = 4 - (len(base64_clean) % 4)
                base64_clean += '=' * padding

            try:
                decoded = base64.b64decode(base64_clean)
                if len(decoded) > 10:
                    attachments.append((mime_type, decoded))
            except Exception:
                continue

        return attachments

    @staticmethod
    def check_base64_patterns(body: bytes) -> bool:
        """Check if body contains base64 encoded data"""
        text = body.decode('utf-8', errors='ignore')
        matches = AttachmentExtractor.BASE64_PATTERN.findall(text)
        return len([m for m in matches if len(m[1]) > AttachmentExtractor.BASE64_MIN_LENGTH]) > 0


class VirusScanStats:
    def __init__(self):
        self._lock = threading.Lock()
        self._total_scanned = 0
        self._total_infected = 0
        self._total_quarantined = 0
        self._total_errors = 0
        self._total_timeouts = 0
        self._total_skipped = 0
        self._scanner_stats: Dict[str, Dict[str, int]] = {}
        self._scan_durations: List[float] = []

    def increment_scanned(self, scanner: str = ""):
        with self._lock:
            self._total_scanned += 1
            if scanner:
                if scanner not in self._scanner_stats:
                    self._scanner_stats[scanner] = {"scanned": 0, "infected": 0}
                self._scanner_stats[scanner]["scanned"] += 1

    def increment_infected(self, scanner: str = ""):
        with self._lock:
            self._total_infected += 1
            if scanner and scanner in self._scanner_stats:
                self._scanner_stats[scanner]["infected"] += 1

    def increment_quarantined(self):
        with self._lock:
            self._total_quarantined += 1

    def increment_errors(self):
        with self._lock:
            self._total_errors += 1

    def increment_timeouts(self):
        with self._lock:
            self._total_timeouts += 1

    def increment_skipped(self):
        with self._lock:
            self._total_skipped += 1

    def record_scan_duration(self, duration_ms: float):
        with self._lock:
            self._scan_durations.append(duration_ms)
            if len(self._scan_durations) > 1000:
                self._scan_durations = self._scan_durations[-1000:]

    def get_stats(self) -> Dict[str, Any]:
        with self._lock:
            avg_duration = (
                sum(self._scan_durations) / len(self._scan_durations)
                if self._scan_durations else 0
            )
            return {
                "total_scanned": self._total_scanned,
                "total_infected": self._total_infected,
                "total_quarantined": self._total_quarantined,
                "total_errors": self._total_errors,
                "total_timeouts": self._total_timeouts,
                "total_skipped": self._total_skipped,
                "avg_scan_duration_ms": round(avg_duration, 2),
                "scanner_stats": self._scanner_stats.copy()
            }


class VirusDatabaseUpdater:
    def __init__(self, scanners: List[VirusScannerInterface], update_interval_hours: int = 24):
        self._scanners = scanners
        self._update_interval = update_interval_hours * 3600
        self._last_update = 0
        self._running = False
        self._thread: Optional[threading.Thread] = None

    def start(self):
        self._running = True
        self._thread = threading.Thread(target=self._update_loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._running = False

    def _update_loop(self):
        while self._running:
            current_time = time.time()
            if current_time - self._last_update >= self._update_interval:
                self._update_all()
                self._last_update = current_time
            time.sleep(300)

    def _update_all(self):
        logger.info("Starting virus database update")
        for scanner in self._scanners:
            try:
                scanner.update_database()
            except Exception as e:
                logger.error(f"Failed to update {scanner.get_name()} database: {e}")
        logger.info("Virus database update completed")

    def force_update(self):
        self._update_all()
        self._last_update = time.time()


class VirusScanEngine:
    def __init__(
        self,
        enabled: bool = True,
        scan_mode: ScanMode = ScanMode.ASYNC,
        scanners: Optional[List[VirusScannerInterface]] = None,
        scan_timeout: int = 5,
        quarantine_exchange: str = "audit.quarantine",
        auto_quarantine: bool = True,
        notify_on_infection: bool = True,
        notify_callbacks: Optional[List[Callable]] = None,
        max_threads: int = 4,
        max_body_size: int = 10485760,
        min_attachment_size: int = 1024,
        skip_large_messages: bool = True
    ):
        self._enabled = enabled
        self._scan_mode = scan_mode
        self._scanners = scanners or []
        self._scan_timeout = scan_timeout
        self._quarantine_exchange = quarantine_exchange
        self._auto_quarantine = auto_quarantine
        self._notify_on_infection = notify_on_infection
        self._notify_callbacks = notify_callbacks or []
        self._max_body_size = max_body_size
        self._min_attachment_size = min_attachment_size
        self._skip_large_messages = skip_large_messages

        self._executor = ThreadPoolExecutor(max_workers=max_threads, thread_name_prefix="virus_scan")
        self._pending_results: Dict[str, Future] = {}
        self._pending_results_lock = threading.Lock()

        self._stats = VirusScanStats()
        self._quarantine_messages: List[QuarantineMessage] = []
        self._quarantine_lock = threading.Lock()

        self._db_updater = VirusDatabaseUpdater(self._scanners)
        self._db_updater.start()

        self._running = True

    @property
    def enabled(self) -> bool:
        return self._enabled

    @property
    def stats(self) -> VirusScanStats:
        return self._stats

    def add_notify_callback(self, callback: Callable):
        self._notify_callbacks.append(callback)

    def remove_notify_callback(self, callback: Callable):
        if callback in self._notify_callbacks:
            self._notify_callbacks.remove(callback)

    def should_scan(self, body: bytes, headers: Dict[str, Any]) -> bool:
        if not self._enabled:
            return False

        if not self._scanners or not any(s.is_available() for s in self._scanners):
            self._stats.increment_skipped()
            return False

        body_size = len(body) if body else 0

        if self._skip_large_messages and body_size > self._max_body_size:
            logger.info(f"Skipping scan for large message ({body_size} bytes)")
            self._stats.increment_skipped()
            return False

        if body_size < 10:
            self._stats.increment_skipped()
            return False

        has_attachment = AttachmentExtractor.check_base64_patterns(body)
        if not has_attachment and body_size < self._min_attachment_size:
            self._stats.increment_skipped()
            return False

        return True

    def scan_sync(
        self,
        message_id: str,
        body: bytes,
        headers: Dict[str, Any]
    ) -> ScanResult:
        start_time = time.time()

        if not self.should_scan(body, headers):
            return ScanResult(
                message_id=message_id,
                is_infected=False,
                scan_duration_ms=0,
                scanner="skipped"
            )

        result = self._scan_data(message_id, body, headers)
        result.scan_duration_ms = (time.time() - start_time) * 1000

        self._stats.increment_scanned(result.scanner)
        self._stats.record_scan_duration(result.scan_duration_ms)

        if result.is_infected:
            self._stats.increment_infected(result.scanner)
            self._handle_infection(message_id, body, headers, result)

        return result

    def scan_async(
        self,
        message_id: str,
        body: bytes,
        headers: Dict[str, Any],
        on_complete: Optional[Callable] = None
    ) -> Optional[Future]:
        if not self.should_scan(body, headers):
            result = ScanResult(
                message_id=message_id,
                is_infected=False,
                scan_duration_ms=0,
                scanner="skipped"
            )
            if on_complete:
                try:
                    on_complete(result)
                except Exception as e:
                    logger.error(f"Async scan callback error: {e}")
            return None

        future = self._executor.submit(self._scan_async_task, message_id, body, headers, on_complete)

        with self._pending_results_lock:
            self._pending_results[message_id] = future

        return future

    def _scan_async_task(
        self,
        message_id: str,
        body: bytes,
        headers: Dict[str, Any],
        on_complete: Optional[Callable]
    ):
        start_time = time.time()

        try:
            result = self._scan_data(message_id, body, headers)
            result.scan_duration_ms = (time.time() - start_time) * 1000

            self._stats.increment_scanned(result.scanner)
            self._stats.record_scan_duration(result.scan_duration_ms)

            if result.is_infected:
                self._stats.increment_infected(result.scanner)
                self._handle_infection(message_id, body, headers, result)

            if on_complete:
                on_complete(result)

            return result

        except Exception as e:
            logger.error(f"Async scan error for {message_id}: {e}")
            result = ScanResult(
                message_id=message_id,
                is_infected=False,
                scan_duration_ms=(time.time() - start_time) * 1000,
                error=str(e)
            )
            if on_complete:
                on_complete(result)
            return result

        finally:
            with self._pending_results_lock:
                self._pending_results.pop(message_id, None)

    def _scan_data(
        self,
        message_id: str,
        body: bytes,
        headers: Dict[str, Any]
    ) -> ScanResult:
        if not self._scanners:
            return ScanResult(
                message_id=message_id,
                is_infected=False,
                scanner="none"
            )

        attachments = AttachmentExtractor.extract_base64_attachments(body)
        scan_data_parts = [body]

        for mime_type, attachment in attachments:
            scan_data_parts.append(attachment)

        for scanner in self._scanners:
            if not scanner.is_available():
                continue

            try:
                for data_part in scan_data_parts:
                    is_infected, virus_name = self._scan_with_timeout(
                        scanner, data_part, self._scan_timeout
                    )

                    if is_infected:
                        return ScanResult(
                            message_id=message_id,
                            is_infected=True,
                            virus_name=virus_name,
                            scanner=scanner.get_name()
                        )

            except TimeoutError:
                logger.warning(f"Scan timeout for {message_id} with {scanner.get_name()}")
                self._stats.increment_timeouts()
                return ScanResult(
                    message_id=message_id,
                    is_infected=False,
                    timeout=True,
                    scanner=scanner.get_name()
                )
            except Exception as e:
                logger.error(f"Scan error for {message_id}: {e}")
                self._stats.increment_errors()
                continue

        return ScanResult(
            message_id=message_id,
            is_infected=False,
            scanner=self._scanners[0].get_name() if self._scanners else "none"
        )

    def _scan_with_timeout(
        self,
        scanner: VirusScannerInterface,
        data: bytes,
        timeout: int
    ) -> Tuple[bool, Optional[str]]:
        import concurrent.futures

        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
            future = executor.submit(scanner.scan, data)
            try:
                return future.result(timeout=timeout)
            except concurrent.futures.TimeoutError:
                raise TimeoutError(f"Scan timed out after {timeout} seconds")

    def _handle_infection(
        self,
        message_id: str,
        body: bytes,
        headers: Dict[str, Any],
        result: ScanResult
    ):
        logger.warning(f"Virus detected in message {message_id}: {result.virus_name}")

        if self._auto_quarantine:
            self._quarantine_message(message_id, body, headers, result)

        if self._notify_on_infection:
            self._notify_callbacks_infection(message_id, body, headers, result)

    def _quarantine_message(
        self,
        message_id: str,
        body: bytes,
        headers: Dict[str, Any],
        result: ScanResult
    ):
        quarantine_msg = QuarantineMessage(
            message_id=message_id,
            virus_name=result.virus_name or "unknown",
            original_exchange=headers.get('x-original-exchange', ''),
            original_routing_key=headers.get('x-original-routing-key', ''),
            body=body,
            headers={**headers, 'x-quarantine-reason': result.virus_name}
        )

        with self._quarantine_lock:
            self._quarantine_messages.append(quarantine_msg)
            self._stats.increment_quarantined()

        logger.info(f"Message {message_id} quarantined due to virus: {result.virus_name}")

    def _notify_callbacks_infection(
        self,
        message_id: str,
        body: bytes,
        headers: Dict[str, Any],
        result: ScanResult
    ):
        for callback in self._notify_callbacks:
            try:
                callback(message_id, body, headers, result)
            except Exception as e:
                logger.error(f"Notify callback error: {e}")

    def get_quarantine_messages(self) -> List[QuarantineMessage]:
        with self._quarantine_lock:
            return self._quarantine_messages.copy()

    def clear_quarantine(self):
        with self._quarantine_lock:
            self._quarantine_messages.clear()

    def get_pending_count(self) -> int:
        with self._pending_results_lock:
            return len(self._pending_results)

    def wait_pending(self, timeout: Optional[float] = None):
        with self._pending_results_lock:
            futures = list(self._pending_results.values())

        for future in futures:
            try:
                future.result(timeout=timeout)
            except Exception:
                pass

    def stop(self):
        self._running = False
        self._db_updater.stop()

        self.wait_pending(timeout=10)

        self._executor.shutdown(wait=True, cancel_futures=True)

    def get_status(self) -> Dict[str, Any]:
        return {
            "enabled": self._enabled,
            "scan_mode": self._scan_mode.value,
            "scanners": [
                {
                    "name": s.get_name(),
                    "available": s.is_available()
                }
                for s in self._scanners
            ],
            "pending_count": self.get_pending_count(),
            "quarantine_count": len(self._quarantine_messages),
            "stats": self._stats.get_stats()
        }
