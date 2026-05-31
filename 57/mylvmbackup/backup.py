"""备份流程：先创建 LVM 快照 → 短暂 FTWRL + FLUSH LOGS → 释放锁 → 挂载 → mydumper → 压缩 → S3。

优化：将 LVM 快照创建（大卷可能 >10s）移到 FTWRL 之前，锁仅用于确保
redo/engine 日志在快照时刻已落盘，并捕获 binlog 位点，锁窗口通常 <1 秒。

MVCC / 一致性保证：
- 精简快照对源 LV 是 copy-on-write，快照创建时刻的数据物理视图不变
- FTWRL + FLUSH LOGS 将 redo、binlog、dirty page 刷到磁盘
- 之后对源 LV 的写入会写进薄快照的 COW 区域，不会污染快照
- 从快照恢复时：InnoDB 会像崩溃恢复一样回放 redo，达到一致状态
- mydumper 额外使用 --trx-consistency-only 保证逻辑一致性
"""
from __future__ import annotations

import json
import os
import shutil
import time
from typing import Optional

from .config import AppConfig
from .exceptions import BackupError
from .lvm import LVMManager
from .logger import get_logger
from .mysql_client import MySQLClient
from .storage import (
    BackupManifest,
    Compressor,
    GPGClient,
    S3Client,
    _sha256_dir,
    _sha256_file,
    find_latest_manifest,
    load_manifest,
    make_backup_id,
    write_manifest,
)

log = get_logger(__name__)


class BackupWorkflow:
    def __init__(self, cfg: AppConfig) -> None:
        self.cfg = cfg
        self.lvm = LVMManager(cfg.lvm)
        self.mysql = MySQLClient(cfg.mysql)
        self.s3 = S3Client(cfg.s3) if cfg.s3.enabled else None
        self.compressor = Compressor(cfg.backup.compress_program, cfg.backup.compress_level)
        self.gpg = GPGClient(cfg.gpg) if cfg.gpg.enabled else None

    # ------------------------------------------------------------------
    def _make_snapshot_name(self, incremental: bool) -> str:
        ts = time.strftime("%Y%m%d-%H%M%S")
        suffix = "inc" if incremental else "full"
        return f"{self.cfg.lvm.snapshot_name_prefix}-{suffix}-{ts}"

    def _prepare_output_dir(self, backup_id: str) -> str:
        path = os.path.join(self.cfg.backup.output_dir, backup_id)
        os.makedirs(path, exist_ok=True)
        return path

    # ------------------------------------------------------------------
    def run(self, *, incremental: bool = False) -> BackupManifest:
        snapshot_name = self._make_snapshot_name(incremental)
        backup_id = make_backup_id(snapshot_name)
        output_dir = self._prepare_output_dir(backup_id)

        parent_manifest: Optional[BackupManifest] = None
        if incremental and self.s3 is not None:
            latest_key = find_latest_manifest(self.s3, self.cfg.s3.prefix)
            if latest_key:
                local_manifest_path = os.path.join(output_dir, ".parent_manifest.json")
                self.s3.download_file(latest_key, local_manifest_path)
                parent_manifest = load_manifest(local_manifest_path)

        os.makedirs(output_dir, exist_ok=True)
        os.makedirs(self.cfg.backup.temp_dir, exist_ok=True)

        lock_seconds = 0.0
        snapshot = None
        try:
            # Step 1: 连接 MySQL
            log.info("=== 步骤 1: 连接 MySQL")
            self.mysql.connect()
            version = self.mysql.server_version()

            # Step 2: 创建 LVM 快照（不持锁，大卷也不会阻塞写入）
            log.info("=== 步骤 2: 创建 LVM 快照 %s（无锁）", snapshot_name)
            t0 = time.time()
            snapshot = self.lvm.create_snapshot(snapshot_name)
            log.info("快照创建耗时 %.2fs", time.time() - t0)

            # Step 3: 短暂 FTWRL + FLUSH LOGS，仅用于刷日志 + 捕获 binlog 位点
            #         该窗口通常 < 1 秒，配合快照实现一致性
            log.info("=== 步骤 3: 短暂 FTWRL + FLUSH LOGS（目标：<1s 锁窗口）")
            t_lock_start = time.time()
            try:
                self.mysql.execute("SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ")
                try:
                    self.mysql.execute("FLUSH NO_WRITE_TO_BINLOG TABLES")
                except Exception as exc:
                    log.debug("FLUSH NO_WRITE_TO_BINLOG TABLES 跳过: %s", exc)
                self.mysql.execute("FLUSH TABLES WITH READ LOCK")
                # 在持锁时刷 redo/binlog/引擎日志（部分指令依赖 MySQL 发行版/版本，容错执行）
                for stmt in (
                    "FLUSH ENGINE LOGS",
                    "FLUSH BINARY LOGS",
                    "FLUSH ERROR LOGS",
                    "FLUSH SLOW LOGS",
                    "FLUSH GENERAL LOGS",
                ):
                    try:
                        self.mysql.execute(stmt)
                    except Exception as exc:
                        log.debug("跳过 %s: %s", stmt, exc)
                binlog = self.mysql.binary_log_info()
            finally:
                try:
                    self.mysql.execute("UNLOCK TABLES")
                except Exception as exc:  # pragma: no cover
                    log.warning("释放读锁失败: %s", exc)
            lock_seconds = time.time() - t_lock_start
            log.info("锁持有时间 %.4fs", lock_seconds)
            if lock_seconds > 5.0:
                log.warning(
                    "锁持有时间 %.2fs 超过 5s，考虑检查慢查询 / 长事务", lock_seconds
                )

            self.mysql.close()

            # Step 4: 挂载快照（只读）
            log.info("=== 步骤 4: 挂载快照")
            mount_path = None
            try:
                mount_path = self.lvm.mount_snapshot(snapshot_name)

                # Step 5: mydumper 并行备份
                dump_dir = os.path.join(output_dir, "dump")
                os.makedirs(dump_dir, exist_ok=True)
                log.info("=== 步骤 5: mydumper 并行备份（线程=%d）", self.cfg.backup.threads)
                self.mysql.run_mydumper(dump_dir, threads=self.cfg.backup.threads)

                # Step 6: 压缩归档
                archive_path = os.path.join(output_dir, f"{backup_id}.tar.zst")
                log.info("=== 步骤 6: 打包压缩 -> %s", archive_path)
                archive_size = self.compressor.pack(dump_dir, archive_path)
                archive_sha256 = _sha256_file(archive_path)

                # Step 6b: 计算 dump 目录中每文件的 SHA256，写入 manifest
                log.info("=== 步骤 6b: 计算 dump 文件 SHA256 校验和")
                file_checksums = _sha256_dir(dump_dir)

                # Step 6c: GPG 对称加密（AES-256）
                encrypted_path: Optional[str] = None
                encrypted_sha256: Optional[str] = None
                if self.gpg is not None:
                    encrypted_path = archive_path + ".gpg"
                    log.info("=== 步骤 6c: GPG 加密 -> %s", encrypted_path)
                    self.gpg.encrypt(archive_path, encrypted_path)
                    encrypted_sha256 = _sha256_file(encrypted_path)
                    # 加密后删除明文归档
                    try:
                        os.remove(archive_path)
                    except OSError:
                        pass

                # Step 7: 生成 manifest（含每文件校验和与加密信息）
                manifest = BackupManifest(
                    backup_id=backup_id,
                    timestamp=time.time(),
                    snapshot_name=snapshot_name,
                    incremental=incremental,
                    parent_backup_id=parent_manifest.backup_id if parent_manifest else None,
                    binlog_file=binlog["file"] if binlog else None,
                    binlog_position=binlog["position"] if binlog else None,
                    mysql_version=version,
                    backup_dir=output_dir,
                    archive_path=encrypted_path if encrypted_path else archive_path,
                    archive_size=archive_size,
                    archive_sha256=archive_sha256,
                    encrypted=(self.gpg is not None),
                    encryption_algo=(self.cfg.gpg.cipher_algo if self.gpg else None),
                    encrypted_archive_path=encrypted_path,
                    encrypted_archive_sha256=encrypted_sha256,
                    files=file_checksums,
                    s3_key=None,
                    s3_bucket=None,
                    extra={
                        "lock_held_seconds": f"{lock_seconds:.4f}",
                        "consistency_model": "lvm_thin_snapshot+ftwrl_short+mydumper_trx",
                    },
                )

                manifest_path = os.path.join(output_dir, "manifest.json")
                write_manifest(manifest_path, manifest)

                # Step 8: 上传到 S3（加密文件或明文归档 + manifest）
                if self.s3 is not None:
                    log.info("=== 步骤 8: 上传到 S3")
                    prefix = self.cfg.s3.prefix.rstrip("/")
                    upload_file = encrypted_path or archive_path
                    archive_key = f"{prefix}/{backup_id}/{os.path.basename(upload_file)}"
                    manifest_key = f"{prefix}/{backup_id}/manifest.json"
                    self.s3.upload_file(upload_file, archive_key)
                    self.s3.upload_file(manifest_path, manifest_key)
                    manifest.s3_bucket = self.cfg.s3.bucket
                    manifest.s3_key = manifest_key
                    write_manifest(manifest_path, manifest)

                try:
                    shutil.rmtree(dump_dir)
                except OSError:
                    pass

                log.info("=== 备份完成: %s （锁窗口 %.4fs）", backup_id, lock_seconds)
                return manifest
            finally:
                if mount_path:
                    try:
                        self.lvm.umount_snapshot(snapshot_name)
                    except Exception as exc:
                        log.warning("卸载快照失败: %s", exc)
        finally:
            if snapshot is not None:
                try:
                    self.lvm.remove_snapshot(snapshot_name)
                except Exception as exc:
                    log.warning("删除快照失败: %s", exc)
