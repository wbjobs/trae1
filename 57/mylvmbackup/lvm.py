"""LVM 管理：创建 / 挂载 / 卸载 / 删除精简快照。

使用 `liblvm` 的 Python 绑定（如果可用），否则退回到 CLI 调用。
"""
from __future__ import annotations

import os
import re
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple

from .config import LVMConfig
from .exceptions import LVMError
from .logger import get_logger
from .utils import run_command, sudo_prefix, ensure_dir, parse_size_to_bytes

log = get_logger(__name__)

try:  # pragma: no cover - liblvm 是可选依赖
    import lvm as _liblvm  # type: ignore
    _HAS_LIBLVM = True
except Exception:  # pragma: no cover
    _liblvm = None
    _HAS_LIBLVM = False


DM_PATH = "/dev/mapper"
_SNAPSHOT_NAME_RE = re.compile(r"^[A-Za-z0-9_.-]+$")


@dataclass
class SnapshotInfo:
    name: str
    vg: str
    lv: str
    origin: str
    size: str
    creation_time: float
    thin: bool

    @property
    def device_path(self) -> str:
        return f"/dev/{self.vg}/{self.name}"

    @property
    def mapper_path(self) -> str:
        return f"{DM_PATH}/{self.vg}-{self.name}".replace("--", "---").replace("-", "--")


class LVMManager:
    """封装 LVM 精简快照相关操作。"""

    def __init__(self, cfg: LVMConfig, *, use_sudo: bool = True) -> None:
        self.cfg = cfg
        self.use_sudo = use_sudo
        self._sudo = sudo_prefix(use_sudo)
        self._check_tools()

    # ------------------------------------------------------------------
    # 工具检测
    # ------------------------------------------------------------------
    def _check_tools(self) -> None:
        for tool in ("lvs", "lvcreate", "lvremove", "mount", "umount"):
            rc = os.system(f"command -v {tool} >/dev/null 2>&1")
            if rc != 0:
                log.warning("未找到命令: %s，LVM 操作可能失败", tool)

    # ------------------------------------------------------------------
    # 基本信息查询
    # ------------------------------------------------------------------
    def _lvs_field(self, vg: str, lv: str, field: str) -> Optional[str]:
        out = run_command(
            self._sudo
            + [
                "lvs",
                "--noheadings",
                "--nosuffix",
                "--units",
                "b",
                "-o",
                field,
                f"{vg}/{lv}",
            ],
            capture=True,
            check=False,
        ).stdout.strip()
        return out or None

    def lv_exists(self, vg: str, lv: str) -> bool:
        res = run_command(
            self._sudo + ["lvs", "--noheadings", "-o", "lv_name", f"{vg}/{lv}"],
            capture=True,
            check=False,
        )
        return res.returncode == 0 and bool(res.stdout.strip())

    def origin_info(self) -> Tuple[bool, Optional[str], int]:
        """返回 (是否为精简卷, 精简池名, 卷大小 bytes)。"""
        attrs = self._lvs_field(self.cfg.vg_name, self.cfg.lv_name, "lv_attr") or ""
        pool = self._lvs_field(self.cfg.vg_name, self.cfg.lv_name, "pool_lv") or None
        size_str = self._lvs_field(self.cfg.vg_name, self.cfg.lv_name, "lv_size") or "0"
        thin = attrs.startswith("V")
        try:
            size = int(float(size_str))
        except ValueError:
            size = 0
        return thin, pool, size

    # ------------------------------------------------------------------
    # 自适应快照大小
    # ------------------------------------------------------------------
    def compute_snapshot_size(self) -> str:
        """根据源 LV 大小与精简池空闲容量决定快照大小。

        - 精简快照：默认使用源 LV 大小的 20%，上限为精简池可用的 80%
        - 传统快照：默认使用源 LV 大小的 20%，上限为 VG 可用的 50%
        用户在配置中指定时直接使用用户值。
        """
        if self.cfg.snapshot_size:
            return self.cfg.snapshot_size

        thin, pool, lv_size = self.origin_info()
        lv_size = max(lv_size, 512 * 1024 * 1024)
        suggest = max(int(lv_size * 0.2), 512 * 1024 * 1024)

        if thin and pool:
            out = run_command(
                self._sudo
                + [
                    "vgs",
                    "--noheadings",
                    "--nosuffix",
                    "--units",
                    "b",
                    "-o",
                    "vg_free",
                    self.cfg.vg_name,
                ],
                capture=True,
            ).stdout.strip()
            try:
                vg_free = int(float(out))
            except ValueError:
                vg_free = suggest
            cap = int(vg_free * 0.8)
            return f"{min(suggest, cap)}B"
        else:
            out = run_command(
                self._sudo
                + [
                    "vgs",
                    "--noheadings",
                    "--nosuffix",
                    "--units",
                    "b",
                    "-o",
                    "vg_free",
                    self.cfg.vg_name,
                ],
                capture=True,
            ).stdout.strip()
            try:
                vg_free = int(float(out))
            except ValueError:
                vg_free = suggest
            cap = int(vg_free * 0.5)
            return f"{min(suggest, cap)}B"

    # ------------------------------------------------------------------
    # 创建 / 删除快照
    # ------------------------------------------------------------------
    def create_snapshot(self, snapshot_name: str) -> SnapshotInfo:
        if not _SNAPSHOT_NAME_RE.match(snapshot_name):
            raise LVMError(f"快照名不合法: {snapshot_name}")

        thin, pool, _ = self.origin_info()
        size = self.compute_snapshot_size()

        if thin and pool:
            cmd = self._sudo + [
                "lvcreate",
                "-s",
                f"{self.cfg.vg_name}/{self.cfg.lv_name}",
                "-n",
                snapshot_name,
                "--thinpool",
                pool,
                "-V",
                size,
                "-y",
            ]
        else:
            cmd = self._sudo + [
                "lvcreate",
                "-s",
                f"{self.cfg.vg_name}/{self.cfg.lv_name}",
                "-n",
                snapshot_name,
                "-L",
                size,
                "-y",
            ]

        log.info("创建 LVM 快照: %s", " ".join(cmd))
        run_command(cmd)

        attrs = self._lvs_field(self.cfg.vg_name, snapshot_name, "lv_attr") or ""
        return SnapshotInfo(
            name=snapshot_name,
            vg=self.cfg.vg_name,
            lv=self.cfg.lv_name,
            origin=f"{self.cfg.vg_name}/{self.cfg.lv_name}",
            size=size,
            creation_time=time.time(),
            thin=bool(thin),
        )

    def remove_snapshot(self, snapshot_name: str) -> None:
        if not self.lv_exists(self.cfg.vg_name, snapshot_name):
            log.debug("快照 %s 已不存在，跳过删除", snapshot_name)
            return
        log.info("删除快照 %s/%s", self.cfg.vg_name, snapshot_name)
        run_command(
            self._sudo
            + [
                "lvremove",
                "-f",
                f"{self.cfg.vg_name}/{snapshot_name}",
            ]
        )

    def list_snapshots(self) -> List[SnapshotInfo]:
        out = run_command(
            self._sudo
            + [
                "lvs",
                "--noheadings",
                "--nosuffix",
                "--units",
                "b",
                "-o",
                "lv_name,origin,lv_size,lv_time,lv_attr,vg_name",
                "--select",
                f"vg_name={self.cfg.vg_name} && lv_attr=~[^V]",
                self.cfg.vg_name,
            ],
            capture=True,
            check=False,
        ).stdout
        snapshots: List[SnapshotInfo] = []
        for line in out.splitlines():
            parts = line.split()
            if len(parts) < 6:
                continue
            name, origin, size_str, tstr, attrs, vg = parts[:6]
            if not name.startswith(self.cfg.snapshot_name_prefix):
                continue
            try:
                ts = time.mktime(time.strptime(tstr, "%Y-%m-%d_%H:%M:%S%z"))  # best effort
            except ValueError:
                ts = time.time()
            snapshots.append(
                SnapshotInfo(
                    name=name,
                    vg=vg,
                    lv=self.cfg.lv_name,
                    origin=origin,
                    size=size_str,
                    creation_time=ts,
                    thin=attrs.startswith("V"),
                )
            )
        return snapshots

    # ------------------------------------------------------------------
    # 挂载 / 卸载
    # ------------------------------------------------------------------
    def mount_point_for(self, snapshot_name: str) -> str:
        return os.path.join(self.cfg.mount_base, snapshot_name)

    def mount_snapshot(self, snapshot_name: str) -> str:
        mp = self.mount_point_for(snapshot_name)
        ensure_dir(mp, use_sudo=self.use_sudo)
        dev = f"/dev/{self.cfg.vg_name}/{snapshot_name}"
        log.info("挂载 %s -> %s", dev, mp)
        cmd = self._sudo + [
            "mount",
            "-o",
            "ro,nouuid,noatime",
            dev,
            mp,
        ]
        try:
            run_command(cmd)
        except Exception:
            # 某些文件系统（如 xfs）需要不同参数
            cmd2 = self._sudo + ["mount", "-o", "ro,noatime,nodiratime", dev, mp]
            try:
                run_command(cmd2)
            except Exception as exc:
                raise LVMError(f"挂载快照失败: {exc}") from exc
        return mp

    def umount_snapshot(self, snapshot_name: str, *, lazy: bool = True) -> None:
        mp = self.mount_point_for(snapshot_name)
        if not os.path.ismount(mp):
            return
        log.info("卸载 %s", mp)
        args = ["umount"]
        if lazy:
            args.append("-l")
        args.append(mp)
        for _ in range(3):
            res = run_command(self._sudo + args, check=False)
            if res.returncode == 0:
                return
            time.sleep(1)
        raise LVMError(f"卸载失败: {mp}")

    # ------------------------------------------------------------------
    # 基于薄快照链的增量
    # ------------------------------------------------------------------
    def latest_snapshot(self) -> Optional[SnapshotInfo]:
        snaps = sorted(self.list_snapshots(), key=lambda s: s.creation_time)
        return snaps[-1] if snaps else None

    def chain_length(self) -> int:
        return len(self.list_snapshots())
