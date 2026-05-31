"""MySQL 交互：FTWRL、一致性快照、mydumper 并行备份、myloader 恢复。"""
from __future__ import annotations

import os
from contextlib import contextmanager
from typing import Generator, List, Optional

from .config import MySQLConfig
from .exceptions import MySQLError
from .logger import get_logger
from .utils import run_command

log = get_logger(__name__)


class MySQLClient:
    """最小化 MySQL 客户端封装，负责 FTWRL / 解锁以及 mydumper 命令构建。"""

    def __init__(self, cfg: MySQLConfig) -> None:
        self.cfg = cfg
        self._conn = None

    def connect(self):
        try:
            import mysql.connector  # type: ignore
        except ImportError as exc:  # pragma: no cover
            raise MySQLError(
                "未安装 mysql-connector-python") from exc

        kwargs = {
            "host": self.cfg.host,
            "port": self.cfg.port,
            "user": self.cfg.user,
            "password": self.cfg.password,
            "autocommit": True,
            "connection_timeout": 30,
        }
        if self.cfg.socket:
            kwargs["unix_socket"] = self.cfg.socket
        self._conn = mysql.connector.connect(**kwargs)
        return self._conn

    def close(self) -> None:
        if self._conn is not None:
            try:
                self._conn.close()
            except Exception:  # pragma: no cover
                pass
            self._conn = None

    def execute(self, sql: str, params: tuple = ()) -> None:
        if self._conn is None:
            self.connect()
        cursor = self._conn.cursor()
        try:
            cursor.execute(sql, params)
        finally:
            cursor.close()

    def fetch_one(self, sql: str, params: tuple = ()):
        if self._conn is None:
            self.connect()
        cursor = self._conn.cursor()
        try:
            cursor.execute(sql, params)
            return cursor.fetchone()
        finally:
            cursor.close()

    @contextmanager
    def flush_read_lock(self, timeout: float = 30.0) -> Generator[None, None, None]:
        """获取全局读锁（FTWRL），离开上下文自动释放。"""
        log.info("执行 FLUSH TABLES WITH READ LOCK ...")
        try:
            self.execute("SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ")
            self.execute("FLUSH NO_WRITE_TO_BINLOG TABLES")
            self.execute("FLUSH TABLES WITH READ LOCK")
        except Exception as exc:
            raise MySQLError(f"FTWRL 失败: {exc}") from exc

        try:
            yield
        finally:
            try:
                self.execute("UNLOCK TABLES")
                log.info("已释放全局读锁")
            except Exception as exc:  # pragma: no cover
                log.warning("释放读锁失败: %s", exc)

    def build_mydumper_cmd(
        self,
        output_dir: str,
        *,
        threads: int = 4,
        extra_args: Optional[List[str]] = None,
    ) -> List[str]:
        cmd: List[str] = ["mydumper"]
        if self.cfg.defaults_file:
            cmd += ["--defaults-file", self.cfg.defaults_file]
        else:
            cmd += ["--host", self.cfg.host, "--port", str(self.cfg.port), "--user", self.cfg.user]
            if self.cfg.password:
                cmd += ["--password", self.cfg.password]
        cmd += [
            "--outputdir",
            output_dir,
            "--threads",
            str(threads),
            "--compress",
            "--build-empty-files",
            "--trx-consistency-only",
            "--less-locking",
            "--use-savepoints",
        ]
        if self.cfg.database:
            cmd += ["--database", self.cfg.database]
        if self.cfg.databases:
            cmd += ["--regex", "|".join(f"^{d}\\." for d in self.cfg.databases)]
        cmd += ["--long-query-guard", "600", "--lock-all-tables"]
        if extra_args:
            cmd.extend(extra_args)
        return cmd

    def run_mydumper(self, output_dir: str, *, threads: int = 4, extra_args: Optional[List[str]] = None, timeout: Optional[float] = None) -> None:
        cmd = self.build_mydumper_cmd(output_dir, threads=threads, extra_args=extra_args)
        env = os.environ.copy()
        if self.cfg.password:
            env["MYSQL_PWD"] = self.cfg.password
        log.info("执行 mydumper（%d 线程）", threads)
        run_command(cmd, env=env, timeout=timeout)

    def build_myloader_cmd(
        self, dump_dir: str, *, threads: int = 4, extra_args: Optional[List[str]] = None) -> List[str]:
        cmd: List[str] = ["myloader"]
        if self.cfg.defaults_file:
            cmd += ["--defaults-file", self.cfg.defaults_file]
        else:
            cmd += ["--host", self.cfg.host, "--port", str(self.cfg.port), "--user", self.cfg.user]
            if self.cfg.password:
                cmd += ["--password", self.cfg.password]
        cmd += ["--directory", dump_dir, "--threads", str(threads), "--overwrite-tables"]
        if self.cfg.database:
            cmd += ["--source-db", self.cfg.database, "--database", self.cfg.database]
        if extra_args:
            cmd.extend(extra_args)
        return cmd

    def run_myloader(self, dump_dir: str, *, threads: int = 4, extra_args: Optional[List[str]] = None, timeout: Optional[float] = None) -> None:
        cmd = self.build_myloader_cmd(dump_dir, threads=threads, extra_args=extra_args)
        env = os.environ.copy()
        if self.cfg.password:
            env["MYSQL_PWD"] = self.cfg.password
        log.info("执行 myloader（%d 线程）", threads)
        run_command(cmd, env=env, timeout=timeout)

    # ------------------------------------------------------------------
    # mysqlcheck
    # ------------------------------------------------------------------
    def build_mysqlcheck_cmd(
        self,
        mysqlcheck_path: str = "mysqlcheck",
        *,
        check_type: str = "all",
        databases: Optional[List[str]] = None,
        extra_args: Optional[List[str]] = None,
    ) -> List[str]:
        """构建 mysqlcheck 命令。

        check_type: all | check | analyze | repair | optimize
        """
        cmd: List[str] = [mysqlcheck_path]
        if self.cfg.defaults_file:
            cmd += ["--defaults-file", self.cfg.defaults_file]
        else:
            cmd += ["--host", self.cfg.host, "--port", str(self.cfg.port), "--user", self.cfg.user]
            if self.cfg.password:
                cmd += ["--password", self.cfg.password]
        if check_type == "all":
            cmd += ["--all-databases", "--check", "--analyze"]
        elif check_type == "check":
            cmd += ["--all-databases", "--check"]
        elif check_type == "analyze":
            cmd += ["--all-databases", "--analyze"]
        elif check_type == "repair":
            cmd += ["--all-databases", "--repair"]
        elif check_type == "optimize":
            cmd += ["--all-databases", "--optimize"]
        if databases:
            cmd = [mysqlcheck_path]
            if self.cfg.defaults_file:
                cmd += ["--defaults-file", self.cfg.defaults_file]
            else:
                cmd += ["--host", self.cfg.host, "--port", str(self.cfg.port), "--user", self.cfg.user]
                if self.cfg.password:
                    cmd += ["--password", self.cfg.password]
            cmd += ["--databases"] + list(databases) + ["--check", "--analyze"]
        if extra_args:
            cmd.extend(extra_args)
        return cmd

    def run_mysqlcheck(
        self,
        mysqlcheck_path: str = "mysqlcheck",
        *,
        check_type: str = "all",
        databases: Optional[List[str]] = None,
        extra_args: Optional[List[str]] = None,
        timeout: Optional[float] = None,
    ) -> str:
        """执行 mysqlcheck 并返回输出。失败时抛出 MySQLError。"""
        cmd = self.build_mysqlcheck_cmd(mysqlcheck_path, check_type=check_type, databases=databases, extra_args=extra_args)
        env = os.environ.copy()
        if self.cfg.password:
            env["MYSQL_PWD"] = self.cfg.password
        log.info("执行 mysqlcheck: %s", " ".join(cmd))
        result = run_command(cmd, env=env, capture=True, timeout=timeout)
        return (result.stdout or "") + (result.stderr or "")

    def binary_log_info(self):
        try:
            row = self.fetch_one("SHOW MASTER STATUS")
            if row:
                return {"file": row[0], "position": row[1]}
        except Exception:
            pass
        return None

    def server_version(self) -> str:
        row = self.fetch_one("SELECT VERSION()")
        return row[0] if row else "unknown"
