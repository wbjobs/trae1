"""异常定义。"""
from __future__ import annotations


class MyLVMBackupError(Exception):
    """所有工具异常的基类。"""


class ConfigError(MyLVMBackupError):
    """配置相关错误。"""


class LVMError(MyLVMBackupError):
    """LVM 操作错误。"""


class MySQLError(MyLVMBackupError):
    """MySQL 操作错误。"""


class BackupError(MyLVMBackupError):
    """备份流程错误。"""


class RestoreError(MyLVMBackupError):
    """恢复流程错误。"""


class S3Error(MyLVMBackupError):
    """S3 上传 / 下载错误。"""


class CommandError(MyLVMBackupError):
    """子命令执行错误。"""

    def __init__(self, message: str, returncode: int = 1, stderr: str = "") -> None:
        super().__init__(message)
        self.returncode = returncode
        self.stderr = stderr
