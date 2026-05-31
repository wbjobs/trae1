import logging
import time
import uuid
from typing import Optional, Tuple
from pathlib import Path

from .dlp_config import QuarantineConfig
from .event_models import FileOperationEvent, OperationType

logger = logging.getLogger(__name__)


class QuarantineManager:
    def __init__(self, config: QuarantineConfig):
        self.config = config

    def _smb_move(self, smb_monitor, src_path: str, dst_path: str) -> bool:
        """Move file within the same SMB share using pysmb rename."""
        if not smb_monitor or not smb_monitor._pysmb_conn:
            logger.error("SMB monitor not connected")
            return False

        share = smb_monitor.config.share_name
        try:
            smb_monitor._pysmb_conn.rename(
                share,
                src_path,
                dst_path,
            )
            logger.info(f"Quarantined: {src_path} -> {dst_path}")
            return True
        except Exception as e:
            logger.error(f"SMB rename failed: {src_path} -> {dst_path}: {e}")
            return False

    def _smb_copy_and_delete(self, smb_monitor, src_path: str, dst_path: str) -> bool:
        """Fallback: read file, write to quarantine, delete source."""
        import io
        if not smb_monitor or not smb_monitor._pysmb_conn:
            return False

        share = smb_monitor.config.share_name
        try:
            buf = io.BytesIO()
            smb_monitor._pysmb_conn.retrieveFile(share, src_path, buf)
            buf.seek(0)

            dst_dir = str(Path(dst_path).parent)
            try:
                smb_monitor._pysmb_conn.createDirectory(share, dst_dir)
            except Exception:
                pass

            smb_monitor._pysmb_conn.storeFileFromOffset(
                share, dst_path, buf, offset=0
            )
            buf.close()

            smb_monitor._pysmb_conn.deleteFiles(share, src_path)
            logger.info(f"Quarantined (copy+delete): {src_path} -> {dst_path}")
            return True
        except Exception as e:
            logger.error(f"SMB copy+delete failed: {e}")
            return False

    def quarantine_file(
        self,
        smb_monitor,
        file_path: str,
        username: str = "",
        reason: str = "",
    ) -> Tuple[bool, str, str]:
        """
        Move file to quarantine directory.
        Returns: (success, quarantine_path, isolation_id)
        """
        if not self.config.enabled:
            return False, "", ""

        isolation_id = str(uuid.uuid4())[:12]
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        file_name = Path(file_path).name
        stem = Path(file_path).stem
        suffix = Path(file_path).suffix

        quarantine_name = f"{stem}.{timestamp}.{isolation_id}{suffix}"
        quarantine_path = f"{self.config.quarantine_path}/{quarantine_name}"

        try:
            smb_monitor._pysmb_conn.createDirectory(
                smb_monitor.config.share_name, self.config.quarantine_path
            )
        except Exception:
            pass

        success = self._smb_move(smb_monitor, file_path, quarantine_path)
        if not success:
            success = self._smb_copy_and_delete(smb_monitor, file_path, quarantine_path)

        if success:
            logger.warning(
                f"FILE QUARANTINED: {file_path} -> {quarantine_path} "
                f"(reason: {reason}, isolation_id: {isolation_id})"
            )

        return success, quarantine_path, isolation_id

    def restore_file(
        self,
        smb_monitor,
        quarantine_path: str,
        original_path: str,
    ) -> bool:
        """Restore a quarantined file back to its original location."""
        if not smb_monitor or not smb_monitor._pysmb_conn:
            return False

        try:
            orig_dir = str(Path(original_path).parent)
            try:
                smb_monitor._pysmb_conn.createDirectory(
                    smb_monitor.config.share_name, orig_dir
                )
            except Exception:
                pass

            success = self._smb_move(smb_monitor, quarantine_path, original_path)
            if success:
                logger.info(f"File restored: {quarantine_path} -> {original_path}")
            return success
        except Exception as e:
            logger.error(f"Restore failed: {e}")
            return False

    def list_quarantined(self, smb_monitor) -> list:
        """List files in the quarantine directory."""
        if not smb_monitor or not smb_monitor._pysmb_conn:
            return []

        result = []
        try:
            entries = smb_monitor._pysmb_conn.listPath(
                smb_monitor.config.share_name,
                self.config.quarantine_path,
            )
            for entry in entries:
                if entry.filename in (".", ".."):
                    continue
                result.append({
                    "name": entry.filename,
                    "is_directory": entry.isDirectory,
                    "size": entry.file_size,
                    "last_write": entry.last_write_time,
                })
        except Exception as e:
            logger.error(f"List quarantine failed: {e}")
        return result

    def should_auto_quarantine(self, severity: str) -> bool:
        return (
            self.config.enabled
            and self.config.auto_quarantine
            and severity in self.config.auto_quarantine_severity
        )
