"""通用工具函数。"""
from __future__ import annotations

import os
import subprocess
import shlex
from typing import List, Optional, Sequence

from .exceptions import CommandError
from .logger import get_logger

log = get_logger(__name__)


def run_command(
    cmd: Sequence[str],
    *,
    check: bool = True,
    input_text: Optional[str] = None,
    capture: bool = False,
    cwd: Optional[str] = None,
    env: Optional[dict] = None,
    timeout: Optional[float] = None,
) -> subprocess.CompletedProcess:
    """执行一个外部命令，失败时抛出 CommandError。"""
    log.debug("$ %s", " ".join(shlex.quote(str(c)) for c in cmd))
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    try:
        result = subprocess.run(
            list(cmd),
            input=input_text,
            capture_output=capture,
            text=True,
            cwd=cwd,
            env=merged_env,
            timeout=timeout,
        )
    except FileNotFoundError as exc:
        raise CommandError(f"命令不存在: {cmd[0]}") from exc
    except subprocess.TimeoutExpired as exc:
        raise CommandError(f"命令超时: {cmd}", returncode=-1) from exc

    if check and result.returncode != 0:
        stderr = (result.stderr or "").strip()
        stdout = (result.stdout or "").strip()
        msg = stderr or stdout or f"命令失败: {cmd}"
        raise CommandError(msg, returncode=result.returncode, stderr=stderr)
    return result


def sudo_prefix(use_sudo: bool = True) -> List[str]:
    if not use_sudo:
        return []
    try:
        if os.geteuid() == 0:  # type: ignore[attr-defined]
            return []
    except AttributeError:  # Windows 等平台
        return []
    return ["sudo", "-n"]


def ensure_dir(path: str, *, use_sudo: bool = False) -> None:
    if os.path.isdir(path):
        return
    if use_sudo:
        run_command(sudo_prefix(use_sudo) + ["mkdir", "-p", path])
    else:
        os.makedirs(path, exist_ok=True)


def parse_size_to_bytes(size: str) -> int:
    size = size.strip().upper()
    units = {"K": 1024, "M": 1024**2, "G": 1024**3, "T": 1024**4, "KB": 1000, "MB": 1000**2, "GB": 1000**3}
    if size.isdigit():
        return int(size)
    for suffix, mult in sorted(units.items(), key=lambda kv: -len(kv[0])):
        if size.endswith(suffix):
            try:
                return int(float(size[: -len(suffix)]) * mult)
            except ValueError:
                break
    raise CommandError(f"无法解析大小: {size}")
