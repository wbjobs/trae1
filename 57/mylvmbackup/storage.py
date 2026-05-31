"""备份存储：压缩、GPG 加密、S3 上传/下载、增量 manifest 管理、完整性校验。"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import tarfile
import time
from dataclasses import asdict, dataclass, field
from typing import Dict, List, Optional

from .config import GPGConfig, S3Config
from .exceptions import S3Error
from .logger import get_logger
from .utils import run_command

log = get_logger(__name__)


@dataclass
class BackupManifest:
    backup_id: str
    timestamp: float
    snapshot_name: str
    incremental: bool
    parent_backup_id: Optional[str]
    binlog_file: Optional[str]
    binlog_position: Optional[int]
    mysql_version: str
    backup_dir: str
    archive_path: str
    archive_size: int
    archive_sha256: str
    encrypted: bool = False
    encryption_algo: Optional[str] = None
    encrypted_archive_path: Optional[str] = None
    encrypted_archive_sha256: Optional[str] = None
    files: Dict[str, Dict[str, object]] = field(default_factory=dict)
    s3_key: Optional[str] = None
    s3_bucket: Optional[str] = None
    extra: Dict[str, str] = field(default_factory=dict)

    def to_json(self) -> str:
        return json.dumps(asdict(self), indent=2, sort_keys=True)

    @classmethod
    def from_json(cls, text: str) -> "BackupManifest":
        data = json.loads(text)
        known = {k: v for k, v in data.items() if k in cls.__annotations__}
        return cls(**known)


def _sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _sha256_dir(root: str) -> Dict[str, Dict[str, object]]:
    """递归计算目录下所有文件的 SHA256 与大小，返回 {相对路径: {size, sha256}}。"""
    result: Dict[str, Dict[str, object]] = {}
    if not os.path.isdir(root):
        return result
    for dirpath, _dirnames, filenames in os.walk(root):
        for fname in filenames:
            full = os.path.join(dirpath, fname)
            rel = os.path.relpath(full, root)
            try:
                st = os.stat(full)
                result[rel] = {
                    "size": st.st_size,
                    "sha256": _sha256_file(full),
                }
            except OSError as exc:
                log.warning("无法计算 %s 的校验和: %s", full, exc)
    return result


def verify_files(root: str, expected: Dict[str, Dict[str, object]]) -> Dict[str, List[str]]:
    """校验目录下的文件是否与 manifest 中的校验和一致。

    返回 {"ok": [...], "missing": [...], "mismatch": [...], "extra": [...]}
    """
    result: Dict[str, List[str]] = {
        "ok": [],
        "missing": [],
        "mismatch": [],
        "extra": [],
    }
    actual_files: set = set()
    for dirpath, _dirnames, filenames in os.walk(root):
        for fname in filenames:
            full = os.path.join(dirpath, fname)
            rel = os.path.relpath(full, root)
            actual_files.add(rel)

    expected_keys = set(expected.keys())

    for rel in sorted(expected_keys - actual_files):
        result["missing"].append(rel)

    for rel in sorted(actual_files - expected_keys):
        result["extra"].append(rel)

    for rel in sorted(expected_keys & actual_files):
        info = expected[rel]
        full = os.path.join(root, rel)
        try:
            actual_sha = _sha256_file(full)
            expected_sha = info.get("sha256", "")
            if actual_sha == expected_sha:
                result["ok"].append(rel)
            else:
                result["mismatch"].append(rel)
                log.warning("校验和不匹配: %s (expected=%s actual=%s)", rel, expected_sha, actual_sha)
        except OSError as exc:
            result["mismatch"].append(rel)
            log.warning("无法读取 %s: %s", rel, exc)

    return result


class GPGClient:
    """GPG 对称加密 / 解密封装。"""

    def __init__(self, cfg: GPGConfig) -> None:
        self.cfg = cfg

    def _passphrase_args(self) -> List[str]:
        if self.cfg.passphrase_file:
            return ["--batch", "--yes", "--passphrase-file", self.cfg.passphrase_file]
        if self.cfg.passphrase:
            return ["--batch", "--yes", "--passphrase", self.cfg.passphrase]
        return ["--batch", "--yes"]

    def encrypt(self, src_path: str, dst_path: str) -> int:
        """使用对称加密（AES-256）加密 src_path，输出到 dst_path，返回加密后大小。"""
        algo = self.cfg.cipher_algo or "AES256"
        cmd = [self.cfg.gpg_binary, "--symmetric", "--cipher-algo", algo]
        cmd += self._passphrase_args()
        if not self.cfg.armor:
            cmd += ["--compress-algo", "none"]
        cmd += ["-o", dst_path, src_path]
        log.info("GPG 加密 %s -> %s (algo=%s)", src_path, dst_path, algo)
        run_command(cmd)
        return os.path.getsize(dst_path)

    def decrypt(self, src_path: str, dst_path: str) -> None:
        """解密 src_path 到 dst_path。"""
        cmd = [self.cfg.gpg_binary, "--decrypt"]
        cmd += self._passphrase_args()
        cmd += ["-o", dst_path, src_path]
        log.info("GPG 解密 %s -> %s", src_path, dst_path)
        run_command(cmd)


class Compressor:
    """将 mydumper 输出目录打包为单个压缩归档文件。"""

    def __init__(self, program: str = "zstd", level: int = 3) -> None:
        self.program = program
        self.level = level

    def pack(self, src_dir: str, dst_path: str) -> int:
        program = self.program.lower()
        if program == "zstd":
            self._tar_pipe(src_dir, dst_path, ["zstd", f"-{self.level}", "-q"])
        elif program in ("gzip", "pigz"):
            self._tar_pipe(src_dir, dst_path, [program, f"-{self.level}"])
        elif program == "lz4":
            self._tar_pipe(src_dir, dst_path, ["lz4", f"-{self.level}"])
        elif program == "none":
            self._tar_pipe(src_dir, dst_path, None)
        else:
            raise S3Error(f"不支持的压缩程序: {program}")
        return os.path.getsize(dst_path)

    def _tar_pipe(self, src_dir: str, dst_path: str, compressor: Optional[List[str]]) -> None:
        parent = os.path.dirname(os.path.abspath(dst_path))
        os.makedirs(parent, exist_ok=True)
        base = os.path.basename(src_dir.rstrip("/"))
        abs_parent_dir = os.path.dirname(os.path.abspath(src_dir.rstrip("/")))
        parent_dir = abs_parent_dir if abs_parent_dir else "."

        tar_cmd = ["tar", "-cf", "-", "-C", parent_dir, base]
        if compressor:
            with open(dst_path, "wb") as out:
                p1 = subprocess.Popen(tar_cmd, stdout=subprocess.PIPE)
                p2 = subprocess.Popen(compressor, stdin=p1.stdout, stdout=out)
                p1.stdout.close()
                rc2 = p2.wait()
                rc1 = p1.wait()
                if rc1 != 0 or rc2 != 0:
                    raise S3Error(f"打包失败 tar={rc1} compressor={rc2}")
        else:
            run_command(tar_cmd + ["-f", dst_path])


class S3Client:
    """基于 boto3 的 S3 封装，支持普通 S3 / S3 兼容存储。"""

    def __init__(self, cfg: S3Config) -> None:
        self.cfg = cfg
        self._client = None

    def _get_client(self):
        if self._client is not None:
            return self._client
        try:
            import boto3
        except ImportError as exc:  # pragma: no cover
            raise S3Error("未安装 boto3") from exc

        if self.cfg.profile:
            from boto3 import Session
            session = Session(profile_name=self.cfg.profile, region_name=self.cfg.region)
            self._client = session.client("s3", endpoint_url=self.cfg.endpoint_url)
            return self._client

        kwargs: Dict[str, object] = {"region_name": self.cfg.region}
        if self.cfg.endpoint_url:
            kwargs["endpoint_url"] = self.cfg.endpoint_url
        if self.cfg.access_key:
            kwargs["aws_access_key_id"] = self.cfg.access_key
        if self.cfg.secret_key:
            kwargs["aws_secret_access_key"] = self.cfg.secret_key
        self._client = boto3.client("s3", **kwargs)
        return self._client

    def upload_file(self, local_path: str, key: str) -> str:
        if not self.cfg.bucket:
            raise S3Error("未配置 S3 bucket")
        log.info("上传 %s -> s3://%s/%s", local_path, self.cfg.bucket, key)
        extra = dict(self.cfg.extra_args or {})
        self._get_client().upload_file(local_path, self.cfg.bucket, key, ExtraArgs=extra or None)
        return f"s3://{self.cfg.bucket}/{key}"

    def download_file(self, key: str, local_path: str) -> None:
        if not self.cfg.bucket:
            raise S3Error("未配置 S3 bucket")
        log.info("下载 s3://%s/%s -> %s", self.cfg.bucket, key, local_path)
        os.makedirs(os.path.dirname(local_path) or ".", exist_ok=True)
        self._get_client().download_file(self.cfg.bucket, key, local_path)

    def list_objects(self, prefix: str) -> List[Dict]:
        if not self.cfg.bucket:
            return []
        paginator = self._get_client().get_paginator("list_objects_v2")
        items: List[Dict] = []
        for page in paginator.paginate(Bucket=self.cfg.bucket, Prefix=prefix):
            for obj in page.get("Contents", []) or []:
                items.append(obj)
        return items

    def head_manifest(self, key: str) -> Optional[dict]:
        try:
            self._get_client().head_object(Bucket=self.cfg.bucket, Key=key)
            return {"Key": key, "Bucket": self.cfg.bucket}
        except Exception:
            return None


def make_backup_id(snapshot_name: str) -> str:
    return f"{snapshot_name}-{int(time.time())}"


def write_manifest(path: str, manifest: BackupManifest) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(manifest.to_json())


def load_manifest(path: str) -> BackupManifest:
    with open(path, "r", encoding="utf-8") as f:
        return BackupManifest.from_json(f.read())


def unpack_archive(archive_path: str, dst_dir: str, program: str = "zstd") -> None:
    os.makedirs(dst_dir, exist_ok=True)
    program = program.lower()
    if program == "zstd":
        decompressor = ["zstd", "-d", "-q"]
    elif program in ("gzip", "pigz"):
        decompressor = ["gunzip"]
    elif program == "lz4":
        decompressor = ["lz4", "-d"]
    else:
        decompressor = None

    if decompressor:
        with open(archive_path, "rb") as fh:
            p1 = subprocess.Popen(decompressor, stdin=fh, stdout=subprocess.PIPE)
            p2 = subprocess.Popen(["tar", "-xf", "-", "-C", dst_dir], stdin=p1.stdout)
            p1.stdout.close()
            rc2 = p2.wait()
            rc1 = p1.wait()
            if rc1 != 0 or rc2 != 0:
                raise S3Error(f"解压失败 {decompressor} tar={rc2}")
    else:
        with tarfile.open(archive_path, "r:") as tf:
            tf.extractall(path=dst_dir)


def find_latest_manifest(s3: S3Client, prefix: str) -> Optional[str]:
    """返回 S3 中最新的 manifest.json 的 key。"""
    items = s3.list_objects(prefix.rstrip("/") + "/")
    manifests = [i["Key"] for i in items if i["Key"].endswith("/manifest.json")]
    if not manifests:
        return None
    manifests.sort()
    return manifests[-1]
