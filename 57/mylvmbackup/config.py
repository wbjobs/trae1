"""配置加载与验证。"""
from __future__ import annotations

import os
from dataclasses import dataclass, field, asdict
from typing import Any, Dict, List, Optional

import yaml

from .exceptions import ConfigError


@dataclass
class MySQLConfig:
    host: str = "127.0.0.1"
    port: int = 3306
    user: str = "root"
    password: str = ""
    socket: Optional[str] = None
    defaults_file: Optional[str] = None
    database: Optional[str] = None
    databases: List[str] = field(default_factory=list)
    tables: List[str] = field(default_factory=list)


@dataclass
class LVMConfig:
    vg_name: str = ""
    lv_name: str = ""
    thin_pool: Optional[str] = None
    snapshot_name_prefix: str = "mysnap"
    snapshot_size: Optional[str] = None
    mount_base: str = "/mnt/mysql-snapshots"
    fs_type: str = "ext4"


@dataclass
class BackupConfig:
    mydumper_path: str = "mydumper"
    myloader_path: str = "myloader"
    mysqlcheck_path: str = "mysqlcheck"
    threads: int = 4
    compress_program: str = "zstd"
    compress_level: int = 3
    output_dir: str = "/var/backups/mysql"
    temp_dir: str = "/var/backups/mysql/tmp"
    retention_days: int = 7
    verify_checksum: bool = True
    verify_mysqlcheck: bool = True


@dataclass
class GPGConfig:
    enabled: bool = False
    gpg_binary: str = "gpg"
    symmetric: bool = True
    cipher_algo: str = "AES256"
    passphrase: str = ""
    passphrase_file: Optional[str] = None
    recipient: Optional[str] = None
    armor: bool = False


@dataclass
class S3Config:
    enabled: bool = False
    bucket: str = ""
    prefix: str = "mysql-backups"
    region: str = "us-east-1"
    endpoint_url: Optional[str] = None
    access_key: Optional[str] = None
    secret_key: Optional[str] = None
    profile: Optional[str] = None
    extra_args: Dict[str, Any] = field(default_factory=dict)


@dataclass
class AppConfig:
    log_level: str = "INFO"
    mysql: MySQLConfig = field(default_factory=MySQLConfig)
    lvm: LVMConfig = field(default_factory=LVMConfig)
    backup: BackupConfig = field(default_factory=BackupConfig)
    s3: S3Config = field(default_factory=S3Config)
    gpg: GPGConfig = field(default_factory=GPGConfig)

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


def _deep_merge(base: Dict[str, Any], override: Dict[str, Any]) -> Dict[str, Any]:
    result = dict(base)
    for key, value in override.items():
        if (
            key in result
            and isinstance(result[key], dict)
            and isinstance(value, dict)
        ):
            result[key] = _deep_merge(result[key], value)
        else:
            result[key] = value
    return result


def _config_from_file(path: str) -> Dict[str, Any]:
    if not os.path.exists(path):
        raise ConfigError(f"配置文件不存在: {path}")
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    if not isinstance(data, dict):
        raise ConfigError("配置文件根节点必须是字典")
    return data


def _coerce_mysql(d: Dict[str, Any]) -> MySQLConfig:
    return MySQLConfig(
        host=d.get("host", "127.0.0.1"),
        port=int(d.get("port", 3306)),
        user=d.get("user", "root"),
        password=d.get("password", "") or os.environ.get("MYSQL_PASSWORD", ""),
        socket=d.get("socket"),
        defaults_file=d.get("defaults_file"),
        database=d.get("database"),
        databases=list(d.get("databases", []) or []),
        tables=list(d.get("tables", []) or []),
    )


def _coerce_lvm(d: Dict[str, Any]) -> LVMConfig:
    return LVMConfig(
        vg_name=d.get("vg_name", "") or os.environ.get("LVM_VG", ""),
        lv_name=d.get("lv_name", "") or os.environ.get("LVM_LV", ""),
        thin_pool=d.get("thin_pool"),
        snapshot_name_prefix=d.get("snapshot_name_prefix", "mysnap"),
        snapshot_size=d.get("snapshot_size"),
        mount_base=d.get("mount_base", "/mnt/mysql-snapshots"),
        fs_type=d.get("fs_type", "ext4"),
    )


def _coerce_backup(d: Dict[str, Any]) -> BackupConfig:
    return BackupConfig(
        mydumper_path=d.get("mydumper_path", "mydumper"),
        myloader_path=d.get("myloader_path", "myloader"),
        mysqlcheck_path=d.get("mysqlcheck_path", "mysqlcheck"),
        threads=int(d.get("threads", 4)),
        compress_program=d.get("compress_program", "zstd"),
        compress_level=int(d.get("compress_level", 3)),
        output_dir=d.get("output_dir", "/var/backups/mysql"),
        temp_dir=d.get("temp_dir", "/var/backups/mysql/tmp"),
        retention_days=int(d.get("retention_days", 7)),
        verify_checksum=bool(d.get("verify_checksum", True)),
        verify_mysqlcheck=bool(d.get("verify_mysqlcheck", True)),
    )


def _coerce_gpg(d: Dict[str, Any]) -> GPGConfig:
    passphrase = d.get("passphrase", "") or os.environ.get("GPG_PASSPHRASE", "")
    return GPGConfig(
        enabled=bool(d.get("enabled", False)),
        gpg_binary=d.get("gpg_binary", "gpg"),
        symmetric=bool(d.get("symmetric", True)),
        cipher_algo=d.get("cipher_algo", "AES256"),
        passphrase=passphrase,
        passphrase_file=d.get("passphrase_file") or os.environ.get("GPG_PASSPHRASE_FILE"),
        recipient=d.get("recipient"),
        armor=bool(d.get("armor", False)),
    )


def _coerce_s3(d: Dict[str, Any]) -> S3Config:
    access_key = d.get("access_key") or os.environ.get("AWS_ACCESS_KEY_ID")
    secret_key = d.get("secret_key") or os.environ.get("AWS_SECRET_ACCESS_KEY")
    return S3Config(
        enabled=bool(d.get("enabled", False)),
        bucket=d.get("bucket", ""),
        prefix=d.get("prefix", "mysql-backups"),
        region=d.get("region", "us-east-1"),
        endpoint_url=d.get("endpoint_url") or os.environ.get("AWS_ENDPOINT_URL"),
        access_key=access_key,
        secret_key=secret_key,
        profile=d.get("profile"),
        extra_args=dict(d.get("extra_args", {}) or {}),
    )


def load_config(path: Optional[str] = None, overrides: Optional[Dict[str, Any]] = None) -> AppConfig:
    data: Dict[str, Any] = {}
    if path:
        data = _config_from_file(path)

    overrides = overrides or {}
    data = _deep_merge(data, overrides)

    mysql = _coerce_mysql(data.get("mysql", {}) or {})
    lvm = _coerce_lvm(data.get("lvm", {}) or {})
    backup = _coerce_backup(data.get("backup", {}) or {})
    s3 = _coerce_s3(data.get("s3", {}) or {})
    gpg = _coerce_gpg(data.get("gpg", {}) or {})

    cfg = AppConfig(
        log_level=str(data.get("log_level", "INFO")).upper(),
        mysql=mysql,
        lvm=lvm,
        backup=backup,
        s3=s3,
        gpg=gpg,
    )
    _validate(cfg)
    return cfg


def _validate(cfg: AppConfig) -> None:
    if not cfg.lvm.vg_name:
        raise ConfigError("必须指定 lvm.vg_name")
    if not cfg.lvm.lv_name:
        raise ConfigError("必须指定 lvm.lv_name")
    if cfg.backup.threads < 1:
        raise ConfigError("backup.threads 必须 >= 1")
    if cfg.s3.enabled and not cfg.s3.bucket:
        raise ConfigError("启用 S3 时必须指定 s3.bucket")
