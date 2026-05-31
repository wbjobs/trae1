"""MySQL binary symbol detection and validation.

Uses nm/readelf to find function addresses in the MySQL binary,
then validates which symbols are actually present and resolvable.
Also supports offset-based probing for stripped binaries without
debug symbols.
"""

import ctypes
import logging
import os
import re
import struct
import subprocess
from typing import Dict, List, Optional, Set, Tuple

logger = logging.getLogger(__name__)


class SymbolNotFound(Exception):
    pass


class OffsetExtractor:
    """Extracts function offsets from binaries using various methods.

    For stripped binaries, uses objdump/elf analysis to find
    function entry points relative to the binary base address.
    """

    @staticmethod
    def extract_from_objdump(binary_path: str, func_pattern: str) -> Optional[int]:
        try:
            result = subprocess.run(
                ["objdump", "-d", "--start-address=0x0", binary_path],
                capture_output=True, text=True, timeout=60
            )
        except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as e:
            logger.warning(f"objdump failed: {e}")
            return None

        pattern = re.compile(rf"^([0-9a-f]+)\s+<.*{re.escape(func_pattern)}.*>:")
        for line in result.stdout.splitlines():
            match = pattern.match(line.strip())
            if match:
                offset = int(match.group(1), 16)
                logger.info(f"Found offset for {func_pattern}: {hex(offset)}")
                return offset

        return None

    @staticmethod
    def extract_from_readelf_symbols(binary_path: str, func_pattern: str) -> Optional[int]:
        try:
            result = subprocess.run(
                ["readelf", "-s", binary_path],
                capture_output=True, text=True, timeout=30
            )
        except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as e:
            logger.warning(f"readelf -s failed: {e}")
            return None

        pattern = re.compile(
            rf"^\s*\d+:\s+([0-9a-f]+)\s+\d+\s+\w+\s+\w+\s+\w+\s+\w+\s+(.*{re.escape(func_pattern)}.*)$",
            re.IGNORECASE
        )
        for line in result.stdout.splitlines():
            match = pattern.match(line.strip())
            if match:
                offset = int(match.group(1), 16)
                if offset > 0:
                    logger.info(f"Found symbol offset for {func_pattern}: {hex(offset)}")
                    return offset

        return None

    @staticmethod
    def extract_from_nm(binary_path: str, func_pattern: str) -> Optional[int]:
        try:
            result = subprocess.run(
                ["nm", "-n", binary_path],
                capture_output=True, text=True, timeout=30
            )
        except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as e:
            logger.warning(f"nm -n failed: {e}")
            return None

        pattern = re.compile(rf"^([0-9a-f]+)\s+\w+\s+(.*{re.escape(func_pattern)}.*)$", re.IGNORECASE)
        for line in result.stdout.splitlines():
            match = pattern.match(line.strip())
            if match:
                offset = int(match.group(1), 16)
                if offset > 0:
                    logger.info(f"Found nm offset for {func_pattern}: {hex(offset)}")
                    return offset

        return None

    @staticmethod
    def extract_from_elf_header(binary_path: str) -> Dict[str, int]:
        info = {}
        try:
            result = subprocess.run(
                ["readelf", "-h", binary_path],
                capture_output=True, text=True, timeout=10
            )
            for line in result.stdout.splitlines():
                line = line.strip()
                if line.startswith("Entry point address:"):
                    parts = line.split(":")
                    if len(parts) >= 2:
                        info["entry_point"] = int(parts[1].strip(), 16)
        except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
            pass

        try:
            result = subprocess.run(
                ["readelf", "-l", binary_path],
                capture_output=True, text=True, timeout=10
            )
            for line in result.stdout.splitlines():
                if "LOAD" in line:
                    parts = line.split()
                    if len(parts) >= 5:
                        try:
                            offset = int(parts[1], 16)
                            vaddr = int(parts[2], 16)
                            if offset == 0:
                                info["base_vaddr"] = vaddr
                                break
                        except ValueError:
                            pass
        except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
            pass

        return info

    @classmethod
    def find_function_offset(cls, binary_path: str, func_pattern: str) -> Optional[int]:
        methods = [
            cls.extract_from_readelf_symbols,
            cls.extract_from_nm,
            cls.extract_from_objdump,
        ]
        for method in methods:
            offset = method(binary_path, func_pattern)
            if offset is not None and offset > 0:
                return offset
        return None


class MySQLSymbolResolver:
    """Resolve MySQL function names to their addresses in the binary.

    Supports:
    - Symbol table lookup via nm/readelf
    - C++ name demangling via c++filt
    - Offset-based probing for stripped binaries
    - Fuzzy matching for version-specific symbol names
    """

    def __init__(self, binary_path: str):
        self.binary_path = binary_path
        self._symbols: Dict[str, int] = {}
        self._demangled: Dict[str, str] = {}
        self._reverse_demangled: Dict[str, str] = {}
        self._offsets_cache: Dict[str, int] = {}
        self._offset_extractor = OffsetExtractor()

    def _run_nm(self) -> str:
        try:
            result = subprocess.run(
                ["nm", "-D", "--defined-only", self.binary_path],
                capture_output=True, text=True, timeout=30
            )
            return result.stdout
        except FileNotFoundError:
            logger.warning("nm not found, trying readelf...")
            return self._run_readelf()
        except Exception as e:
            logger.error(f"nm failed: {e}")
            return ""

    def _run_readelf(self) -> str:
        try:
            result = subprocess.run(
                ["readelf", "-Ws", self.binary_path],
                capture_output=True, text=True, timeout=30
            )
            return result.stdout
        except Exception as e:
            logger.error(f"readelf failed: {e}")
            return ""

    def _run_demangle(self, mangled: str) -> str:
        try:
            result = subprocess.run(
                ["c++filt", mangled],
                capture_output=True, text=True, timeout=5
            )
            return result.stdout.strip()
        except Exception:
            return mangled

    def _run_demangle_batch(self, mangled_names: List[str]) -> Dict[str, str]:
        if not mangled_names:
            return {}
        try:
            input_text = "\n".join(mangled_names) + "\n"
            result = subprocess.run(
                ["c++filt"],
                input=input_text, capture_output=True, text=True, timeout=10
            )
            demangled_lines = result.stdout.strip().split("\n")
            result_dict = {}
            for orig, demangled in zip(mangled_names, demangled_lines):
                if demangled and demangled != orig:
                    result_dict[orig] = demangled
            return result_dict
        except Exception:
            return {}

    def load_symbols(self) -> Dict[str, int]:
        raw = self._run_nm()
        if not raw:
            raw = self._run_readelf()

        mangled_to_collect = []
        parsed_lines = []

        for line in raw.splitlines():
            parts = line.strip().split()
            if len(parts) < 3:
                continue
            try:
                addr = int(parts[0], 16)
            except ValueError:
                continue
            sym_type = parts[1] if len(parts) > 1 else ""
            name = parts[-1]

            if sym_type.upper() in ("T", "W", "t", "w"):
                self._symbols[name] = addr
                parsed_lines.append(name)
                if name.startswith("_Z") or name.startswith("_Zn"):
                    mangled_to_collect.append(name)

        demangled_batch = self._run_demangle_batch(mangled_to_collect)
        for mangled, demangled in demangled_batch.items():
            self._demangled[demangled] = mangled
            self._reverse_demangled[mangled] = demangled

        logger.info(
            f"Loaded {len(self._symbols)} symbols from {self.binary_path} "
            f"({len(self._demangled)} demangled)"
        )
        return self._symbols

    def resolve(self, func_name: str) -> Optional[str]:
        if func_name in self._symbols:
            return func_name

        if func_name in self._reverse_demangled:
            return self._reverse_demangled[func_name]

        for demangled, mangled in self._demangled.items():
            if func_name == demangled or func_name in demangled:
                return mangled

        for demangled, mangled in self._demangled.items():
            if demangled.startswith(func_name) or func_name.startswith(demangled.split("(")[0]):
                return mangled

        pattern = re.compile(re.escape(func_name))
        for name in self._symbols:
            if pattern.search(name):
                return name

        return None

    def resolve_many(self, func_names: List[str]) -> Dict[str, str]:
        resolved = {}
        for name in func_names:
            mangled = self.resolve(name)
            if mangled:
                resolved[name] = mangled
            else:
                logger.warning(f"Symbol not found: {name}")
        return resolved

    def resolve_with_fallback(
        self,
        logical_name: str,
        candidates: List[str],
    ) -> Tuple[Optional[str], List[str]]:
        for candidate in candidates:
            resolved = self.resolve(candidate)
            if resolved:
                return resolved, candidates

        fuzzy = self.find_contains(
            logical_name.split("::")[-1] if "::" in logical_name else logical_name
        )
        for f in fuzzy:
            r = self.resolve(f)
            if r:
                return r, fuzzy[:5]

        return None, list(dict.fromkeys(candidates + [logical_name]))

    def find_contains(self, keyword: str) -> List[str]:
        matches = []
        kw = keyword.lower()
        for name in self._symbols:
            if kw in name.lower():
                matches.append(name)
        for demangled in self._demangled:
            if kw in demangled.lower():
                matches.append(demangled)
        return sorted(set(matches))

    def find_exact_or_prefix(self, keyword: str) -> List[str]:
        matches = []
        kw = keyword.lower()
        for name in self._symbols:
            if name.lower() == kw or name.lower().startswith(kw):
                matches.append(name)
        for demangled in self._demangled:
            if demangled.lower() == kw or demangled.lower().startswith(kw):
                matches.append(demangled)
        return sorted(set(matches))

    def validate_probe(self, func_name: str) -> bool:
        mangled = self.resolve(func_name)
        if not mangled:
            return False
        addr = self._symbols.get(mangled)
        return addr is not None and addr > 0

    def get_offset(self, func_name: str) -> Optional[int]:
        mangled = self.resolve(func_name)
        if mangled and mangled in self._symbols:
            return self._symbols[mangled]

        if func_name in self._offsets_cache:
            return self._offsets_cache[func_name]

        offset = self._offset_extractor.find_function_offset(
            self.binary_path, func_name
        )
        if offset:
            self._offsets_cache[func_name] = offset
        return offset

    def get_symbol_table_summary(self) -> str:
        lines = [f"Binary: {self.binary_path}"]
        lines.append(f"Total symbols: {len(self._symbols)}")
        lines.append(f"Demangled C++ symbols: {len(self._demangled)}")

        categories = {
            "dispatch": [],
            "execute": [],
            "lock": [],
            "io": [],
            "innodb": [],
            "storage": [],
        }

        kw_map = {
            "dispatch": ["dispatch", "do_command"],
            "execute": ["execute", "Sql_cmd", "mysql_parse"],
            "lock": ["lock", "row_mysql"],
            "io": ["fil_io", "os_file", "log_write"],
            "innodb": ["row0sel", "btr_", "trx_"],
            "storage": ["ha_innodb", "ha_innobase", "handler"],
        }

        for name in self._symbols:
            for cat, keywords in kw_map.items():
                for kw in keywords:
                    if kw.lower() in name.lower():
                        categories[cat].append(name)
                        break

        for cat, names in categories.items():
            lines.append(f"\n  {cat.upper()} ({len(names)} found):")
            for n in sorted(names)[:15]:
                demangled = self._reverse_demangled.get(n, n)
                if demangled != n:
                    lines.append(f"    - {n}  ({demangled})")
                else:
                    lines.append(f"    - {n}")
            if len(names) > 15:
                lines.append(f"    ... and {len(names) - 15} more")

        return "\n".join(lines)

    def get_detection_report(
        self, logical_names: List[str], version_key: str = "unknown"
    ) -> str:
        lines = [
            f"Symbol Detection Report",
            f"=======================",
            f"Binary: {self.binary_path}",
            f"MySQL Version Key: {version_key}",
            f"Total symbols loaded: {len(self._symbols)}",
            f"",
        ]

        for name in logical_names:
            mangled = self.resolve(name)
            addr = self._symbols.get(mangled, 0) if mangled else 0
            status = "OK" if mangled and addr > 0 else "MISSING"
            lines.append(f"  [{status}] {name}")
            if mangled:
                lines.append(f"         -> {mangled} @ {hex(addr)}")
                demangled = self._reverse_demangled.get(mangled)
                if demangled and demangled != mangled:
                    lines.append(f"         -> (demangled: {demangled})")
            else:
                candidates = self.find_contains(
                    name.split("::")[-1] if "::" in name else name
                )
                if candidates:
                    lines.append(f"         Candidates: {candidates[:5]}")

        return "\n".join(lines)
