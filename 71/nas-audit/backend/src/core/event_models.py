import time
from dataclasses import dataclass, field
from typing import Optional
from enum import Enum


class OperationType(str, Enum):
    CREATE = "create"
    DELETE = "delete"
    RENAME = "rename"
    MODIFY = "modify"
    ACCESS = "access"


@dataclass
class FileOperationEvent:
    operation_type: OperationType
    file_path: str
    timestamp: float
    username: str = ""
    source_ip: str = ""
    file_size: int = 0
    old_file_path: Optional[str] = None
    file_extension: str = ""

    def to_dict(self) -> dict:
        return {
            "operation_type": self.operation_type.value,
            "file_path": self.file_path,
            "old_file_path": self.old_file_path,
            "timestamp": self.timestamp,
            "username": self.username,
            "source_ip": self.source_ip,
            "file_size": self.file_size,
            "file_extension": self.file_extension,
            "@timestamp": time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(self.timestamp)),
        }
