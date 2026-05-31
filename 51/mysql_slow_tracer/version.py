"""MySQL version detection and version-specific function symbol mapping.

Handles symbol name differences between MySQL 5.7 and 8.0 by:
1. Detecting MySQL version from binary /proc/PID/exe or mysqld --version
2. Providing version-specific function name aliases
3. Allowing manual overrides via configuration
"""

import logging
import os
import re
import subprocess
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple

logger = logging.getLogger(__name__)


MYSQL_VERSIONS = ["5.5", "5.6", "5.7", "8.0"]

FUNCTION_ALIASES: Dict[str, Dict[str, List[str]]] = {
    "5.7": {
        "dispatch_command": [
            "dispatch_command",
            "_Z16dispatch_commandP3THDPK8COM_DATA19enum_server_command",
        ],
        "do_command": [
            "do_command",
            "_Z10do_commandP3THD",
        ],
        "mysql_execute_command": [
            "mysql_execute_command",
            "_Z21mysql_execute_commandP3THD",
            "_Z21mysql_execute_commandP3THDb",
        ],
        "mysql_parse": [
            "mysql_parse",
            "_Z11mysql_parseP3THDPcjP12Parser_state",
            "_Z11mysql_parseP3THDPcjP12Parser_statebb",
        ],
        "mysql_lock_tables": [
            "mysql_lock_tables",
            "_Z17mysql_lock_tablesP3THDP5TABLEj",
            "_Z17mysql_lock_tablesP3THDPP5TABLEjb",
        ],
        "ha_innobase::index_read": [
            "ha_innobase::index_read",
            "_ZN12ha_innobase10index_readEPhj5ha_rkey_mode",
        ],
        "ha_innobase::index_next": [
            "ha_innobase::index_next",
            "_ZN12ha_innobase10index_nextEPh",
        ],
        "ha_innobase::general_fetch": [
            "ha_innobase::general_fetch",
            "_ZN12ha_innobase13general_fetchEPh5ha_rkey_modej",
        ],
        "row_search_mvcc": [
            "row_search_mvcc",
            "_Z15row_search_mvccPh15page_cur_mode_tP14row_prebuilt_tmm",
        ],
        "btr_cur_search_to_nth_level": [
            "btr_cur_search_to_nth_level",
            "_Z27btr_cur_search_to_nth_levelP9dict_index_tmPhjP10btr_cur_tmjbP11mtr_t",
            "_Z27btr_cur_search_to_nth_levelP9dict_index_tmPhjP10btr_cur_tmjbmP11mtr_t",
        ],
        "btr_pcur_open_with_no_init": [
            "btr_pcur_open_with_no_init",
            "_Z25btr_pcur_open_with_no_initP9dict_index_tPhjP10btr_pcur_tmjbP11mtr_t",
        ],
        "fil_io": [
            "fil_io",
            "_Z6fil_io10fil_io_tjyjP5page_tP11fil_node_tP9que_thr_t",
        ],
        "os_file_read": [
            "os_file_read",
            "_Z12os_file_readiPPKcPhim",
        ],
        "os_file_write": [
            "os_file_write",
            "_Z13os_file_writeiPPKcPhim",
        ],
        "log_write_up_to": [
            "log_write_up_to",
            "_Z15log_write_up_toP9log_tmP12Log_handlebm",
        ],
        "mtr_t::commit": [
            "mtr_t::commit",
            "_ZN5mtr_t6commitEv",
        ],
        "trx_commit_for_mysql": [
            "trx_commit_for_mysql",
            "_Z20trx_commit_for_mysqlP5trx_t",
        ],
        "trx_rollback_for_mysql": [
            "trx_rollback_for_mysql",
            "_Z23trx_rollback_for_mysqlP5trx_t",
        ],
        "lock_sec_rec_read_check_and_lock": [
            "lock_sec_rec_read_check_and_lock",
            "_Z29lock_sec_rec_read_check_and_lock6ul_mP11buf_block_tP9dict_index_tmP12que_thr_tP10mtr_t",
        ],
        "lock_table_ix_resurrect": [
            "lock_table_ix_resurrect",
            "_Z23lock_table_ix_resurrectP9lock_t",
        ],
        "row_mysql_handle_errors": [
            "row_mysql_handle_errors",
            "_Z22row_mysql_handle_errorsP10row_prebuilt_tP12que_thr_t",
        ],
    },
    "8.0": {
        "dispatch_command": [
            "dispatch_command",
            "_Z16dispatch_commandP3THDPK8COM_DATA19enum_server_command",
            "dispatch_command(THD*, COM_DATA const*, enum_server_command)",
        ],
        "do_command": [
            "do_command",
            "_Z10do_commandP3THD",
        ],
        "mysql_execute_command": [
            "mysql_execute_command",
            "_Z21mysql_execute_commandP3THD",
            "_Z21mysql_execute_commandP3THDb",
        ],
        "Sql_cmd::execute": [
            "Sql_cmd::execute",
            "_ZN7Sql_cmd7executeEP3THD",
        ],
        "Sql_cmd_dml::execute_inner": [
            "Sql_cmd_dml::execute_inner",
            "_ZN11Sql_cmd_dml13execute_innerEP3THD",
        ],
        "Sql_cmd_select::execute_inner": [
            "Sql_cmd_select::execute_inner",
            "_ZN14Sql_cmd_select13execute_innerEP3THD",
        ],
        "mysql_parse": [
            "mysql_parse",
            "_Z11mysql_parseP3THDPcjP12Parser_state",
            "_Z11mysql_parseP3THDPcjP12Parser_statebb",
        ],
        "mysql_lock_tables": [
            "mysql_lock_tables",
            "_Z17mysql_lock_tablesP3THDP5TABLEj",
            "_Z17mysql_lock_tablesP3THDPP5TABLEjb",
        ],
        "ha_innodb::index_read": [
            "ha_innodb::index_read",
            "_ZN11ha_innodb10index_readEPhj5ha_rkey_mode",
            "_ZN11ha_innodb10index_readEPhjj5ha_rkey_mode",
        ],
        "ha_innodb::index_next": [
            "ha_innodb::index_next",
            "_ZN11ha_innodb10index_nextEPh",
        ],
        "ha_innodb::general_fetch": [
            "ha_innodb::general_fetch",
            "_ZN11ha_innodb13general_fetchEPh5ha_rkey_modej",
        ],
        "row_search_mvcc": [
            "row_search_mvcc",
            "_Z15row_search_mvccPh15page_cur_mode_tP14row_prebuilt_tmm",
        ],
        "btr_cur_search_to_nth_level": [
            "btr_cur_search_to_nth_level",
            "_Z27btr_cur_search_to_nth_levelP9dict_index_tmPhjP10btr_cur_tmjbP11mtr_t",
            "_Z27btr_cur_search_to_nth_levelP9dict_index_tmPhjP10btr_cur_tmjbmP11mtr_t",
        ],
        "btr_pcur_open_with_no_init": [
            "btr_pcur_open_with_no_init",
            "_Z25btr_pcur_open_with_no_initP9dict_index_tPhjP10btr_pcur_tmjbP11mtr_t",
        ],
        "fil_io": [
            "fil_io",
            "_Z6fil_io10fil_io_tjyjP5page_tP11fil_node_tP9que_thr_t",
        ],
        "os_file_read": [
            "os_file_read",
            "_Z12os_file_readiPPKcPhim",
            "_Z12os_file_readP10OS_FILE   ",
        ],
        "os_file_write": [
            "os_file_write",
            "_Z13os_file_writeiPPKcPhim",
        ],
        "log_write_up_to": [
            "log_write_up_to",
            "_Z15log_write_up_toP9log_tmP12Log_handlebm",
            "_Z15log_write_up_tomP12Log_handlebm",
        ],
        "mtr_t::commit": [
            "mtr_t::commit",
            "_ZN5mtr_t6commitEv",
        ],
        "trx_commit_for_mysql": [
            "trx_commit_for_mysql",
            "_Z20trx_commit_for_mysqlP5trx_t",
        ],
        "trx_rollback_for_mysql": [
            "trx_rollback_for_mysql",
            "_Z23trx_rollback_for_mysqlP5trx_t",
        ],
        "lock_sec_rec_read_check_and_lock": [
            "lock_sec_rec_read_check_and_lock",
            "_Z29lock_sec_rec_read_check_and_lock6ul_mP11buf_block_tP9dict_index_tmP12que_thr_tP10mtr_t",
        ],
        "lock_table_ix_resurrect": [
            "lock_table_ix_resurrect",
            "_Z23lock_table_ix_resurrectP9lock_t",
        ],
        "row_mysql_handle_errors": [
            "row_mysql_handle_errors",
            "_Z22row_mysql_handle_errorsP10row_prebuilt_tP12que_thr_t",
        ],
    },
}

IO_FUNCTION_ALIASES: Dict[str, Dict[str, List[str]]] = {
    "5.7": {
        "fil_io": ["fil_io", "_Z6fil_io10fil_io_tjyjP5page_tP11fil_node_tP9que_thr_t"],
        "os_file_read": ["os_file_read", "_Z12os_file_readiPPKcPhim"],
        "os_file_write": ["os_file_write", "_Z13os_file_writeiPPKcPhim"],
    },
    "8.0": {
        "fil_io": ["fil_io", "_Z6fil_io10fil_io_tjyjP5page_tP11fil_node_tP9que_thr_t"],
        "os_file_read": ["os_file_read", "_Z12os_file_readiPPKcPhim"],
        "os_file_write": ["os_file_write", "_Z13os_file_writeiPPKcPhim"],
    },
}

LOCK_FUNCTION_ALIASES: Dict[str, Dict[str, List[str]]] = {
    "5.7": {
        "row_mysql_handle_errors": ["row_mysql_handle_errors", "_Z22row_mysql_handle_errorsP10row_prebuilt_tP12que_thr_t"],
        "lock_sec_rec_read_check_and_lock": ["lock_sec_rec_read_check_and_lock", "_Z29lock_sec_rec_read_check_and_lock6ul_mP11buf_block_tP9dict_index_tmP12que_thr_tP10mtr_t"],
        "lock_table_ix_resurrect": ["lock_table_ix_resurrect", "_Z23lock_table_ix_resurrectP9lock_t"],
    },
    "8.0": {
        "row_mysql_handle_errors": ["row_mysql_handle_errors", "_Z22row_mysql_handle_errorsP10row_prebuilt_tP12que_thr_t"],
        "lock_sec_rec_read_check_and_lock": ["lock_sec_rec_read_check_and_lock", "_Z29lock_sec_rec_read_check_and_lock6ul_mP11buf_block_tP9dict_index_tmP12que_thr_tP10mtr_t"],
        "lock_table_ix_resurrect": ["lock_table_ix_resurrect", "_Z23lock_table_ix_resurrectP9lock_t"],
    },
}

CRITICAL_FUNCTIONS = ["dispatch_command", "mysql_execute_command"]


@dataclass
class DetectedMySQLVersion:
    major: str = ""
    minor: str = ""
    full: str = ""
    source: str = ""

    @property
    def version_key(self) -> str:
        return self.major

    def is_supported(self) -> bool:
        return self.major in MYSQL_VERSIONS


class MySQLVersionDetector:
    """Detects MySQL version and provides version-specific function mappings."""

    def __init__(self, binary_path: str, pid: int = 0):
        self.binary_path = binary_path
        self.pid = pid
        self._version: Optional[DetectedMySQLVersion] = None

    def detect(self) -> DetectedMySQLVersion:
        if self._version is not None:
            return self._version

        self._version = self._detect_version()
        logger.info(
            f"Detected MySQL version: {self._version.major}.{self._version.minor} "
            f"(full: {self._version.full}, source: {self._version.source})"
        )
        return self._version

    def _detect_version(self) -> DetectedMySQLVersion:
        methods = [
            self._detect_from_proc_exe,
            self._detect_from_binary_exec,
            self._detect_from_binary_strings,
            self._detect_from_file_command,
        ]

        for method in methods:
            result = method()
            if result.is_supported():
                return result

        logger.warning(
            "Could not detect MySQL version, defaulting to 8.0 mappings"
        )
        return DetectedMySQLVersion(major="8.0", minor="0", full="unknown", source="default")

    def _detect_from_proc_exe(self) -> DetectedMySQLVersion:
        if not self.pid:
            return DetectedMySQLVersion()

        exe_path = f"/proc/{self.pid}/exe"
        if not os.path.exists(exe_path):
            return DetectedMySQLVersion()

        try:
            link = os.readlink(exe_path)
            return self._parse_from_path(link, source="proc_exe")
        except (OSError, PermissionError):
            return DetectedMySQLVersion()

    def _detect_from_binary_exec(self) -> DetectedMySQLVersion:
        if not os.path.exists(self.binary_path):
            return DetectedMySQLVersion()

        try:
            result = subprocess.run(
                [self.binary_path, "--version"],
                capture_output=True, text=True, timeout=10
            )
            output = result.stdout + result.stderr
            return self._parse_version_string(output, source="--version")
        except (subprocess.TimeoutExpired, OSError, PermissionError):
            return DetectedMySQLVersion()

    def _detect_from_binary_strings(self) -> DetectedMySQLVersion:
        if not os.path.exists(self.binary_path):
            return DetectedMySQLVersion()

        try:
            result = subprocess.run(
                ["strings", self.binary_path],
                capture_output=True, text=True, timeout=15
            )
            for line in result.stdout.splitlines():
                if "mysqld" in line.lower() or "mysql" in line.lower():
                    ver = self._parse_version_string(line, source="strings")
                    if ver.is_supported():
                        return ver
        except (subprocess.TimeoutExpired, OSError, PermissionError):
            pass

        return DetectedMySQLVersion()

    def _detect_from_file_command(self) -> DetectedMySQLVersion:
        if not os.path.exists(self.binary_path):
            return DetectedMySQLVersion()

        try:
            result = subprocess.run(
                ["file", self.binary_path],
                capture_output=True, text=True, timeout=10
            )
            return self._parse_version_string(result.stdout, source="file")
        except (subprocess.TimeoutExpired, OSError):
            return DetectedMySQLVersion()

    def _parse_from_path(self, path: str, source: str) -> DetectedMySQLVersion:
        basename = os.path.basename(path)
        match = re.search(r"mysqld[-_]?(\d+)\.(\d+)", basename)
        if match:
            major = f"{match.group(1)}.{match.group(2)}"
            return DetectedMySQLVersion(
                major=major, minor=match.group(2),
                full=f"{major}.0", source=source
            )
        return DetectedMySQLVersion()

    def _parse_version_string(self, text: str, source: str) -> DetectedMySQLVersion:
        patterns = [
            r"mysqld\s+Ver\s+(\d+)\.(\d+)\.(\d+)",
            r"mysqld[-_]?(\d+)\.(\d+)",
            r"MySQL\s+(\d+)\.(\d+)\.(\d+)",
            r"MariaDB\s+(\d+)\.(\d+)\.(\d+)",
            r"(\d+)\.(\d+)\.(\d+)",
        ]

        for pattern in patterns:
            match = re.search(pattern, text, re.IGNORECASE)
            if match:
                groups = match.groups()
                major_str = f"{groups[0]}.{groups[1]}"
                if major_str in MYSQL_VERSIONS:
                    return DetectedMySQLVersion(
                        major=major_str,
                        minor=groups[1],
                        full=f"{groups[0]}.{groups[1]}.{groups[2]}" if len(groups) >= 3 else major_str,
                        source=source,
                    )

        return DetectedMySQLVersion()

    def get_function_aliases(self) -> Dict[str, List[str]]:
        version = self.detect()
        key = version.version_key
        return FUNCTION_ALIASES.get(key, FUNCTION_ALIASES["8.0"])

    def get_io_aliases(self) -> Dict[str, List[str]]:
        version = self.detect()
        key = version.version_key
        return IO_FUNCTION_ALIASES.get(key, IO_FUNCTION_ALIASES["8.0"])

    def get_lock_aliases(self) -> Dict[str, List[str]]:
        version = self.detect()
        key = version.version_key
        return LOCK_FUNCTION_ALIASES.get(key, LOCK_FUNCTION_ALIASES["8.0"])

    def get_all_aliases_flat(self) -> Dict[str, List[str]]:
        func_aliases = self.get_function_aliases()
        io_aliases = self.get_io_aliases()
        lock_aliases = self.get_lock_aliases()
        merged: Dict[str, List[str]] = {}
        merged.update(func_aliases)
        for k, v in io_aliases.items():
            merged.setdefault(k, []).extend(v)
        for k, v in lock_aliases.items():
            merged.setdefault(k, []).extend(v)
        return merged

    def get_critical_aliases(self) -> Dict[str, List[str]]:
        all_aliases = self.get_all_aliases_flat()
        return {k: v for k, v in all_aliases.items() if k in CRITICAL_FUNCTIONS}


@dataclass
class FunctionMapping:
    """Maps a logical function name to resolved symbol(s) and optional offset."""

    logical_name: str
    resolved_symbol: str = ""
    offset: int = 0
    available_symbols: List[str] = field(default_factory=list)

    def has_resolved(self) -> bool:
        return bool(self.resolved_symbol) or self.offset > 0

    def to_dict(self) -> Dict:
        return {
            "logical_name": self.logical_name,
            "resolved_symbol": self.resolved_symbol,
            "offset": hex(self.offset) if self.offset else None,
            "available_symbols": self.available_symbols,
        }


class FunctionMapper:
    """Resolves logical function names to actual symbols using version info
    and manual overrides from config.
    """

    def __init__(
        self,
        version_detector: MySQLVersionDetector,
        symbol_resolver,
        func_aliases: Optional[Dict[str, str]] = None,
        func_offsets: Optional[Dict[str, int]] = None,
    ):
        self.version_detector = version_detector
        self.symbol_resolver = symbol_resolver
        self.func_aliases = func_aliases or {}
        self.func_offsets = func_offsets or {}
        self._mappings: Dict[str, FunctionMapping] = {}
        self._alias_table = self.version_detector.get_all_aliases_flat()

    def resolve(self, logical_name: str) -> FunctionMapping:
        if logical_name in self._mappings:
            return self._mappings[logical_name]

        mapping = FunctionMapping(logical_name=logical_name)

        if logical_name in self.func_offsets:
            mapping.offset = self.func_offsets[logical_name]
            mapping.resolved_symbol = f"__offset__{hex(mapping.offset)}"
            self._mappings[logical_name] = mapping
            logger.debug(
                f"Using manual offset for {logical_name}: {hex(mapping.offset)}"
            )
            return mapping

        if logical_name in self.func_aliases:
            aliased = self.func_aliases[logical_name]
            resolved = self.symbol_resolver.resolve(aliased)
            if resolved:
                mapping.resolved_symbol = resolved
                mapping.available_symbols = [resolved]
                self._mappings[logical_name] = mapping
                logger.debug(
                    f"Resolved {logical_name} via alias '{aliased}' -> {resolved}"
                )
                return mapping

        version_aliases = self._alias_table.get(logical_name, [logical_name])

        for candidate in version_aliases:
            resolved = self.symbol_resolver.resolve(candidate)
            if resolved:
                mapping.resolved_symbol = resolved
                mapping.available_symbols.append(resolved)
                break

        if not mapping.resolved_symbol:
            all_candidates = version_aliases + [logical_name]
            mapping.available_symbols = list(dict.fromkeys(all_candidates))

            found = self.symbol_resolver.find_contains(
                logical_name.split("::")[-1] if "::" in logical_name else logical_name
            )
            if found:
                mapping.available_symbols = found[:10]
                for f in found:
                    r = self.symbol_resolver.resolve(f)
                    if r:
                        mapping.resolved_symbol = r
                        break

        self._mappings[logical_name] = mapping
        if mapping.resolved_symbol:
            logger.debug(f"Resolved {logical_name} -> {mapping.resolved_symbol}")
        else:
            logger.warning(
                f"Could not resolve {logical_name}. "
                f"Available candidates: {mapping.available_symbols[:5]}"
            )

        return mapping

    def resolve_many(self, logical_names: List[str]) -> Dict[str, FunctionMapping]:
        return {name: self.resolve(name) for name in logical_names}

    def get_mapping_summary(self) -> str:
        version = self.version_detector.detect()
        lines = [f"MySQL Version: {version.major} (source: {version.source})"]
        lines.append("")
        lines.append(f"{'Function':<35} {'Resolved Symbol':<50} {'Offset':<12}")
        lines.append("-" * 100)

        for name, mapping in sorted(self._mappings.items()):
            sym = mapping.resolved_symbol or "(unresolved)"
            off = hex(mapping.offset) if mapping.offset else "-"
            sym_display = sym[:47] + "..." if len(sym) > 50 else sym
            lines.append(f"  {name:<33} {sym_display:<50} {off:<12}")

        unresolved = [n for n, m in self._mappings.items() if not m.has_resolved()]
        if unresolved:
            lines.append("")
            lines.append(f"Unresolved functions ({len(unresolved)}):")
            for n in unresolved:
                lines.append(f"  - {n}")
                mapping = self._mappings[n]
                if mapping.available_symbols:
                    lines.append(f"    Candidates: {mapping.available_symbols[:3]}")

        return "\n".join(lines)
