import logging
import io
import time
from typing import Optional, Tuple

from .dlp_config import SensitiveConfig

logger = logging.getLogger(__name__)


class TikaExtractor:
    def __init__(self, config: SensitiveConfig):
        self.config = config
        self._available = None

    def is_available(self) -> bool:
        if self._available is not None:
            return self._available
        try:
            import requests as req
            resp = req.get(f"{self.config.tika_server}/tika", timeout=5)
            self._available = resp.status_code == 200
        except Exception as e:
            logger.warning(f"Tika server not available at {self.config.tika_server}: {e}")
            self._available = False
        return self._available

    def reset_availability(self):
        self._available = None

    def _should_scan(self, file_path: str, file_size: int) -> bool:
        if not self.is_available():
            return False
        if file_size > self.config.max_file_size_mb * 1024 * 1024:
            logger.debug(f"File too large for scan: {file_path} ({file_size} bytes)")
            return False
        from pathlib import Path
        ext = Path(file_path).suffix.lower()
        return ext in self.config.scan_extensions

    def extract_from_bytes(self, file_content: bytes, file_path: str) -> Optional[str]:
        """Extract text from in-memory bytes via Tika /tika endpoint."""
        if not self._should_scan(file_path, len(file_content)):
            return None

        try:
            import requests as req
            from pathlib import Path

            ext = Path(file_path).suffix.lower()
            headers = {"Accept": "text/plain; charset=utf-8"}
            if ext:
                content_type_map = {
                    ".doc": "application/msword",
                    ".docx": "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
                    ".pdf": "application/pdf",
                    ".txt": "text/plain",
                    ".xls": "application/vnd.ms-excel",
                    ".xlsx": "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
                    ".ppt": "application/vnd.ms-powerpoint",
                    ".pptx": "application/vnd.openxmlformats-officedocument.presentationml.presentation",
                    ".csv": "text/csv",
                    ".rtf": "application/rtf",
                }
                ct = content_type_map.get(ext)
                if ct:
                    headers["Content-Type"] = ct

            resp = req.put(
                f"{self.config.tika_server}/tika",
                data=file_content,
                headers=headers,
                timeout=self.config.tika_timeout,
            )
            resp.encoding = resp.apparent_encoding or "utf-8"
            text = resp.text

            if resp.status_code != 200:
                logger.warning(f"Tika returned {resp.status_code} for {file_path}")
                return None

            if len(text) < self.config.min_text_length:
                return None

            if len(text) > self.config.max_text_length:
                text = text[: self.config.max_text_length]

            logger.debug(f"Extracted {len(text)} chars from {file_path}")
            return text

        except Exception as e:
            logger.error(f"Tika extraction error for {file_path}: {e}")
            return None

    def extract_from_smb_path(self, smb_monitor, file_path: str) -> Tuple[Optional[str], int]:
        """Read file via pysmb and extract text. Returns (text, file_size)."""
        if not smb_monitor or not smb_monitor._pysmb_conn:
            return None, 0

        from pathlib import Path
        ext = Path(file_path).suffix.lower()
        if ext not in self.config.scan_extensions:
            return None, 0

        buf = io.BytesIO()
        try:
            attr = smb_monitor._pysmb_conn.getAttributes(
                smb_monitor.config.share_name, file_path
            )
            file_size = attr.file_size if hasattr(attr, "file_size") else attr.file_size

            if file_size > self.config.max_file_size_mb * 1024 * 1024:
                return None, file_size

            smb_monitor._pysmb_conn.retrieveFile(
                smb_monitor.config.share_name, file_path, buf
            )
            content = buf.getvalue()
            text = self.extract_from_bytes(content, file_path)
            return text, len(content)
        except Exception as e:
            logger.error(f"Failed to read SMB file {file_path}: {e}")
            return None, 0
        finally:
            buf.close()
