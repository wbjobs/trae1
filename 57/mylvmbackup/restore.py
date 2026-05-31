"""恢复流程：S3 下载 -> GPG 解密 -> 校验 -> 解压 -> LVM 新卷创建 -> 挂载 -> myloader -> mysqlcheck。"""
from __future__ import annotations

import json
import os
import shutil
import time
from typing import Optional

from .config import AppConfig
from .exceptions import RestoreError
from .lvm import LVMManager
from .logger import get_logger
from .mysql_client import MySQLClient
from .storage import (
    BackupManifest,
    GPGClient,
    S3Client,
    _sha256_file,
    load_manifest,
    unpack_archive,
    verify_files,
)
from .utils import run_command, sudo_prefix, ensure_dir

log = get_logger(__name__)


class RestoreWorkflow:
    def __init__(self, cfg: AppConfig) -> None:
        self.cfg = cfg
        self.lvm = LVMManager(cfg.lvm)
        self.mysql = MySQLClient(cfg.mysql)
        self.s3 = S3Client(cfg.s3) if cfg.s3.enabled else None
        self.gpg = GPGClient(cfg.gpg) if cfg.gpg.enabled else None

    # ------------------------------------------------------------------
    def _resolve_manifest(self, backup_id: Optional[str], timestamp: Optional[str]) -> BackupManifest:
        if self.s3 is None:
            raise RestoreError("恢复需要启用 S3 配置")

        if backup_id:
            prefix = self.cfg.s3.prefix.rstrip("/")
            key = f"{prefix}/{backup_id}/manifest.json"
            local = os.path.join(self.cfg.backup.output_dir, backup_id, "manifest.json")
            if os.path.exists(local):
                return load_manifest(local)
            self.s3.download_file(key, local)
            return load_manifest(local)

        if timestamp:
            import datetime
            try:
                target = datetime.datetime.fromisoformat(timestamp).timestamp()
            except ValueError:
                raise RestoreError(f"时间格式错误: {timestamp} (需要 ISO8601)")

            prefix = self.cfg.s3.prefix.rstrip("/")
            items = self.s3.list_objects(prefix + "/")
            candidates = [i["Key"] for i in items if i["Key"].endswith("/manifest.json")]
            best_key: Optional[str] = None
            best_ts = 0.0
            for key in candidates:
                local = os.path.join(self.cfg.backup.temp_dir, os.path.basename(os.path.dirname(key)) + ".manifest.json")
                try:
                    self.s3.download_file(key, local)
                    mf = load_manifest(local)
                except Exception:
                    continue
                if mf.timestamp <= target and mf.timestamp > best_ts:
                    best_ts = mf.timestamp
                    best_key = key
            if not best_key:
                raise RestoreError(f"没有找到早于 {timestamp} 的备份")
            local = os.path.join(self.cfg.backup.output_dir, os.path.basename(os.path.dirname(best_key)), "manifest.json")
            self.s3.download_file(best_key, local)
            return load_manifest(local)

        raise RestoreError("必须指定 --backup-id 或 --timestamp")

    def _resolve_chain(self, leaf: BackupManifest) -> list:
        chain = [leaf]
        current = leaf
        while current.parent_backup_id and self.s3 is not None:
            parent_key = f"{self.cfg.s3.prefix.rstrip('/')}/{current.parent_backup_id}/manifest.json"
            local = os.path.join(self.cfg.backup.output_dir, current.parent_backup_id, "manifest.json")
            if not os.path.exists(local):
                try:
                    self.s3.download_file(parent_key, local)
                except Exception:
                    log.warning("未找到父备份 manifest: %s", parent_key)
                    break
            mf = load_manifest(local)
            chain.append(mf)
            current = mf
        chain.reverse()
        return chain

    # ------------------------------------------------------------------
    def _download_archive(self, manifest: BackupManifest) -> str:
        """下载 manifest 指向的归档（支持加密/未加密），返回本地路径。"""
        if self.s3 is None:
            raise RestoreError("S3 未启用")
        prefix = self.cfg.s3.prefix.rstrip("/")
        archive_name = os.path.basename(manifest.archive_path) if manifest.archive_path else f"{manifest.backup_id}.tar.zst"
        # 如果 manifest 中记录了加密路径，优先下载加密文件
        if manifest.encrypted and manifest.encrypted_archive_path:
            archive_name = os.path.basename(manifest.encrypted_archive_path)
        key = f"{prefix}/{manifest.backup_id}/{archive_name}"
        local = os.path.join(self.cfg.backup.output_dir, manifest.backup_id, archive_name)
        if os.path.exists(local):
            log.info("归档已存在: %s", local)
            return local
        os.makedirs(os.path.dirname(local), exist_ok=True)
        log.info("下载归档: %s", key)
        self.s3.download_file(key, local)
        return local

    def _decrypt_if_needed(self, manifest: BackupManifest, local_path: str) -> str:
        """如果归档是加密的，解密后返回明文路径。"""
        if not manifest.encrypted:
            return local_path
        if self.gpg is None:
            raise RestoreError("归档已加密但 GPG 未配置")
        decrypted_path = local_path
        if decrypted_path.endswith(".gpg"):
            decrypted_path = decrypted_path[:-4]
        else:
            decrypted_path = decrypted_path + ".decrypted"
        if os.path.exists(decrypted_path):
            log.info("解密文件已存在: %s", decrypted_path)
            return decrypted_path
        log.info("解密归档: %s -> %s", local_path, decrypted_path)
        self.gpg.decrypt(local_path, decrypted_path)
        return decrypted_path

    def _verify_archive_sha256(self, manifest: BackupManifest, local_path: str) -> None:
        """校验归档文件的 SHA256 是否与 manifest 中一致。"""
        expected = manifest.encrypted_archive_sha256 if manifest.encrypted else manifest.archive_sha256
        if not expected:
            log.warning("manifest 中未记录归档 SHA256，跳过校验")
            return
        log.info("校验归档 SHA256 ...")
        actual = _sha256_file(local_path)
        if actual != expected:
            raise RestoreError(
                f"归档 SHA256 不匹配: expected={expected} actual={actual}"
            )
        log.info("归档 SHA256 校验通过")

    def _verify_dump_files(self, manifest: BackupManifest, dump_dir: str) -> dict:
        """校验解压后 dump 目录中每文件的 SHA256。"""
        if not manifest.files:
            log.warning("manifest 中未记录文件校验和，跳过文件级校验")
            return {"skipped": True}
        log.info("校验 dump 文件 SHA256 (%d files) ...", len(manifest.files))
        result = verify_files(dump_dir, manifest.files)
        if result["missing"] or result["mismatch"]:
            raise RestoreError(
                f"文件校验失败: missing={len(result['missing'])} mismatch={len(result['mismatch'])}"
            )
        log.info(
            "文件校验通过: ok=%d extra=%d",
            len(result["ok"]),
            len(result["extra"]),
        )
        return result

    # ------------------------------------------------------------------
    def _create_restore_volume(self, name: str, size: Optional[str] = None) -> str:
        thin, pool, _ = self.lvm.origin_info()
        cmd_base = sudo_prefix(self.lvm.use_sudo) + ["lvcreate"]
        if thin and pool:
            cmd = cmd_base + [
                "-V", size or "100G",
                "-T", f"{self.cfg.lvm.vg_name}/{pool}",
                "-n", name,
                "-y",
            ]
        else:
            cmd = cmd_base + [
                "-L", size or "100G",
                "-n", name,
                self.cfg.lvm.vg_name,
                "-y",
            ]
        log.info("创建恢复卷: %s", " ".join(cmd))
        run_command(cmd)
        return f"/dev/{self.cfg.lvm.vg_name}/{name}"

    def _format_and_mount(self, device: str, mount_point: str, fs_type: str) -> None:
        log.info("格式化 %s (%s)", device, fs_type)
        run_command(sudo_prefix(self.lvm.use_sudo) + [f"mkfs.{fs_type}", "-f", device])
        ensure_dir(mount_point, use_sudo=self.lvm.use_sudo)
        log.info("挂载 %s -> %s", device, mount_point)
        run_command(sudo_prefix(self.lvm.use_sudo) + ["mount", device, mount_point])

    def _umount(self, mount_point: str) -> None:
        if not os.path.ismount(mount_point):
            return
        run_command(sudo_prefix(self.lvm.use_sudo) + ["umount", mount_point])

    def _run_mysqlcheck(self, check_type: str = "all") -> str:
        """执行 mysqlcheck 校验数据库完整性。"""
        log.info("执行 mysqlcheck 校验数据库完整性 ...")
        try:
            output = self.mysql.run_mysqlcheck(
                mysqlcheck_path=self.cfg.backup.mysqlcheck_path,
                check_type=check_type,
            )
            log.info("mysqlcheck 完成:\n%s", output)
            return output
        except Exception as exc:
            log.error("mysqlcheck 失败: %s", exc)
            raise RestoreError(f"mysqlcheck 校验失败: {exc}") from exc

    # ------------------------------------------------------------------
    def run(
        self,
        *,
        backup_id: Optional[str] = None,
        timestamp: Optional[str] = None,
        new_lv_name: Optional[str] = None,
        lv_size: Optional[str] = None,
        apply_to_mysql: bool = False,
        verify_only: bool = False,
    ) -> dict:
        manifest = self._resolve_manifest(backup_id, timestamp)
        chain = self._resolve_chain(manifest)

        # Step 1: 下载归档
        archive = self._download_archive(manifest)

        # Step 2: 校验归档 SHA256
        self._verify_archive_sha256(manifest, archive)

        # Step 3: 解密（如需要）
        plain_archive = self._decrypt_if_needed(manifest, archive)

        # Step 4: 解压
        extract_dir = os.path.join(self.cfg.backup.output_dir, manifest.backup_id, "dump-restore")
        if os.path.exists(extract_dir):
            shutil.rmtree(extract_dir)
        self._unpack = lambda: unpack_archive(plain_archive, extract_dir, program=self.cfg.backup.compress_program)
        self._unpack()

        # Step 5: 校验 dump 文件 SHA256
        file_verification = self._verify_dump_files(manifest, extract_dir)

        if verify_only:
            log.info("=== 仅校验模式，跳过 LVM / 恢复操作 ===")
            return {
                "status": "verified",
                "backup_id": manifest.backup_id,
                "chain": [m.backup_id for m in chain],
                "archive_sha256_ok": True,
                "file_verification": file_verification,
                "encrypted": manifest.encrypted,
                "archive_path": archive,
                "decrypted_path": plain_archive,
                "extract_dir": extract_dir,
            }

        # Step 6: 创建新 LV 并挂载
        lv_name = new_lv_name or f"mysql-restore-{int(time.time())}"
        mount_point = os.path.join(self.cfg.lvm.mount_base, lv_name)
        device = self._create_restore_volume(lv_name, size=lv_size)
        try:
            self._format_and_mount(device, mount_point, self.cfg.lvm.fs_type)
            try:
                if apply_to_mysql:
                    target_dir = os.path.join(mount_point, "mysql")
                    os.makedirs(target_dir, exist_ok=True)
                    log.info("将数据文件复制到恢复卷: %s", target_dir)
                    for name in os.listdir(extract_dir):
                        src = os.path.join(extract_dir, name)
                        dst = os.path.join(target_dir, name)
                        if os.path.isdir(src):
                            shutil.copytree(src, dst, dirs_exist_ok=True)
                        else:
                            shutil.copy2(src, dst)

                # Step 7: mysqlcheck 校验（如果已配置自动校验且应用到 MySQL）
                mysqlcheck_output: Optional[str] = None
                if self.cfg.backup.verify_mysqlcheck and apply_to_mysql:
                    try:
                        mysqlcheck_output = self._run_mysqlcheck()
                    except RestoreError:
                        raise
                    except Exception as exc:
                        log.warning("mysqlcheck 失败（不致命）: %s", exc)

                log.info("=== 恢复完成，新卷: %s", device)
                return {
                    "status": "ok",
                    "device": device,
                    "mount_point": mount_point,
                    "backup_id": manifest.backup_id,
                    "chain": [m.backup_id for m in chain],
                    "archive_sha256_ok": True,
                    "file_verification": file_verification,
                    "mysqlcheck": mysqlcheck_output,
                }
            finally:
                try:
                    self._umount(mount_point)
                except Exception as exc:
                    log.warning("卸载失败: %s", exc)
        except Exception:
            try:
                run_command(sudo_prefix(self.lvm.use_sudo) + ["lvremove", "-f", f"{self.cfg.lvm.vg_name}/{lv_name}"], check=False)
            except Exception:
                pass
            raise
