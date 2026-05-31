"""CLI 入口。"""
from __future__ import annotations

import json
import logging
import sys
from typing import Optional

import click

from . import __version__
from .backup import BackupWorkflow
from .config import load_config
from .exceptions import MyLVMBackupError
from .logger import get_logger, parse_log_level
from .restore import RestoreWorkflow

log = get_logger(__name__)


@click.group(help="MySQL LVM 热备份 / 恢复 CLI 工具")
@click.version_option(__version__, "-V", "--version")
@click.option("-c", "--config", "config_path", help="YAML 配置文件路径", type=click.Path(dir_okay=False))
@click.option("-v", "--verbose", is_flag=True, help="输出 DEBUG 日志")
@click.option("--log-level", type=click.Choice(["DEBUG", "INFO", "WARNING", "ERROR"]), default=None)
@click.pass_context
def cli(ctx: click.Context, config_path: Optional[str], verbose: bool, log_level: Optional[str]) -> None:
    level = parse_log_level(log_level)
    if verbose:
        level = logging.DEBUG
    from .logger import set_global_level
    set_global_level(level)

    cfg = load_config(config_path)
    if verbose or log_level:
        cfg.log_level = logging.getLevelName(level)
    ctx.obj = cfg


@cli.command("backup", help="执行一次热备份")
@click.option("--incremental/--full", default=False, help="使用上一个快照作为父（增量）")
@click.option("--threads", type=int, default=None, help="覆盖 mydumper 线程数")
@click.option("--output-dir", type=click.Path(file_okay=False), default=None)
@click.pass_obj
def backup_cmd(cfg, incremental: bool, threads: Optional[int], output_dir: Optional[str]) -> None:
    if threads is not None:
        cfg.backup.threads = threads
    if output_dir:
        cfg.backup.output_dir = output_dir
    workflow = BackupWorkflow(cfg)
    try:
        manifest = workflow.run(incremental=incremental)
        click.echo(json.dumps({
            "status": "ok",
            "backup_id": manifest.backup_id,
            "archive": manifest.archive_path,
            "s3_key": manifest.s3_key,
            "binlog_file": manifest.binlog_file,
            "binlog_position": manifest.binlog_position,
        }, indent=2, ensure_ascii=False))
    except MyLVMBackupError as exc:
        click.echo(f"[error] {exc}", err=True)
        sys.exit(2)


@cli.command("restore", help="从指定备份恢复到新 LVM 卷")
@click.option("--backup-id", default=None, help="备份 ID")
@click.option("--timestamp", default=None, help="恢复到的时间点（ISO8601），取最近备份")
@click.option("--new-lv", default=None, help="新 LV 名称，自动生成则留空")
@click.option("--lv-size", default=None, help="新 LV 大小，如 100G")
@click.option("--apply/--no-apply", default=False, help="将解压出的数据复制到新 LV 中")
@click.option("--verify-only", is_flag=True, default=False, help="仅校验完整性（SHA256 + 文件列表），不执行恢复")
@click.option("--gpg-passphrase-file", default=None, help="GPG 对称加密口令文件路径（覆盖配置）")
@click.option("--no-mysqlcheck", is_flag=True, default=False, help="恢复后不执行 mysqlcheck")
@click.pass_obj
def restore_cmd(cfg, backup_id: Optional[str], timestamp: Optional[str], new_lv: Optional[str], lv_size: Optional[str], apply: bool, verify_only: bool, gpg_passphrase_file: Optional[str], no_mysqlcheck: bool) -> None:
    if not backup_id and not timestamp:
        raise click.UsageError("必须指定 --backup-id 或 --timestamp")
    if gpg_passphrase_file:
        cfg.gpg.passphrase_file = gpg_passphrase_file
    if no_mysqlcheck:
        cfg.backup.verify_mysqlcheck = False
    workflow = RestoreWorkflow(cfg)
    try:
        result = workflow.run(
            backup_id=backup_id,
            timestamp=timestamp,
            new_lv_name=new_lv,
            lv_size=lv_size,
            apply_to_mysql=apply,
            verify_only=verify_only,
        )
        click.echo(json.dumps(result, indent=2, ensure_ascii=False, default=str))
    except MyLVMBackupError as exc:
        click.echo(f"[error] {exc}", err=True)
        sys.exit(2)


@cli.command("config", help="打印当前解析后的配置")
@click.pass_obj
def config_cmd(cfg) -> None:
    from dataclasses import asdict
    # 脱敏
    data = asdict(cfg)
    if data.get("mysql", {}).get("password"):
        data["mysql"]["password"] = "***"
    if data.get("s3", {}).get("secret_key"):
        data["s3"]["secret_key"] = "***"
    click.echo(json.dumps(data, indent=2, ensure_ascii=False))


def main() -> None:  # pragma: no cover
    try:
        cli(standalone_mode=True)
    except MyLVMBackupError as exc:
        click.echo(f"[fatal] {exc}", err=True)
        sys.exit(2)


if __name__ == "__main__":  # pragma: no cover
    main()
