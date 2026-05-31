"""Configuration management for MySQL slow query tracer.

Supports manual function name overrides and offset-based probing
for compatibility across MySQL 5.7, 8.0, and stripped binaries.
"""

import json
import os
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Optional


@dataclass
class Config:
    """Tool configuration with MySQL version-aware defaults."""

    pid: int = 0
    threshold_ms: int = 100
    sample_rate: int = 49
    max_stack_depth: int = 64
    mysql_binary: str = "/usr/sbin/mysqld"
    output_dir: str = "./reports"
    report_format: str = "html"
    duration: int = 60
    verbose: bool = False
    capture_io: bool = True
    capture_locks: bool = True
    max_events: int = 10000
    stack_map_size: int = 65536
    events_map_size: int = 65536

    mysql_version: str = ""

    func_aliases: Dict[str, str] = field(default_factory=dict)
    func_offsets: Dict[str, int] = field(default_factory=dict)

    mysql_funcs: List[str] = field(default_factory=lambda: [
        "dispatch_command",
        "do_command",
        "mysql_execute_command",
        "Sql_cmd_dml::execute_inner",
        "Sql_cmd_select::execute_inner",
        "mysql_lock_tables",
        "ha_innodb::index_read",
        "ha_innobase::index_read",
        "ha_innodb::index_next",
        "ha_innobase::index_next",
        "ha_innodb::general_fetch",
        "ha_innobase::general_fetch",
        "row_search_mvcc",
        "btr_cur_search_to_nth_level",
        "btr_pcur_open_with_no_init",
        "fil_io",
        "os_file_read",
        "os_file_write",
        "log_write_up_to",
        "mtr_t::commit",
        "trx_commit_for_mysql",
        "trx_rollback_for_mysql",
    ])

    io_funcs: List[str] = field(default_factory=lambda: [
        "fil_io",
        "os_file_read",
        "os_file_write",
    ])

    lock_funcs: List[str] = field(default_factory=lambda: [
        "row_mysql_handle_errors",
        "lock_sec_rec_read_check_and_lock",
        "lock_table_ix_resurrect",
    ])

    def get_effective_version(self) -> str:
        if self.mysql_version:
            return self.mysql_version
        return "8.0"

    def validate(self) -> List[str]:
        errors = []
        if self.pid <= 0:
            errors.append("PID must be a positive integer")
        if self.threshold_ms < 1:
            errors.append("Threshold must be >= 1ms")
        if self.sample_rate < 1 or self.sample_rate > 999:
            errors.append("Sample rate must be between 1 and 999 Hz")
        if not os.path.isfile(self.mysql_binary):
            errors.append(f"MySQL binary not found: {self.mysql_binary}")
        if self.func_offsets:
            for name, offset in self.func_offsets.items():
                if not isinstance(offset, int) or offset <= 0:
                    errors.append(
                        f"Invalid offset for '{name}': must be positive integer"
                    )
        return errors

    def to_json(self) -> str:
        data = asdict(self)
        data["func_offsets"] = {
            k: hex(v) for k, v in self.func_offsets.items()
        }
        return json.dumps(data, indent=2)

    @classmethod
    def from_json_file(cls, path: str) -> "Config":
        with open(path, "r") as f:
            data = json.load(f)
        offsets = data.get("func_offsets", {})
        if offsets:
            parsed_offsets = {}
            for k, v in offsets.items():
                if isinstance(v, str) and v.startswith("0x"):
                    parsed_offsets[k] = int(v, 16)
                elif isinstance(v, int):
                    parsed_offsets[k] = v
                else:
                    parsed_offsets[k] = int(v)
            data["func_offsets"] = parsed_offsets
        return cls(**data)

    def save(self, path: str):
        dirname = os.path.dirname(path)
        if dirname:
            os.makedirs(dirname, exist_ok=True)
        with open(path, "w") as f:
            f.write(self.to_json())

    def generate_detection_config(self) -> Dict:
        return {
            "pid": self.pid,
            "mysql_binary": self.mysql_binary,
            "mysql_version": self.mysql_version,
            "func_aliases": self.func_aliases,
            "func_offsets": {k: hex(v) for k, v in self.func_offsets.items()},
        }
