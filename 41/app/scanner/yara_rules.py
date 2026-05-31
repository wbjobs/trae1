import os
import glob
import time
import logging
import threading
import yara
from typing import Dict, List, Optional, Set
from dataclasses import dataclass, field

logger = logging.getLogger(__name__)


@dataclass
class YaraRule:
    name: str
    description: str
    severity: str
    category: str


@dataclass
class RuleFileInfo:
    filepath: str
    namespace: str
    last_modified: float = 0.0
    rules_count: int = 0


class YaraRuleLoader:
    def __init__(self, rules_dir: str):
        self.rules_dir = os.path.abspath(rules_dir)
        self._lock = threading.RLock()
        self._compiled_rules: Optional[yara.Rules] = None
        self._rules_metadata: Dict[str, YaraRule] = {}
        self._rule_files: Dict[str, RuleFileInfo] = {}
        self._last_reload_time: float = 0.0
        self._reload_count: int = 0
        self._load_errors: List[str] = []

    def _collect_rule_files(self) -> List[str]:
        patterns = [
            os.path.join(self.rules_dir, "*.yar"),
            os.path.join(self.rules_dir, "*.yara"),
        ]
        files = []
        for pattern in patterns:
            files.extend(glob.glob(pattern))
        return sorted(files)

    def _get_namespace(self, filepath: str) -> str:
        return os.path.splitext(os.path.basename(filepath))[0]

    def _parse_rule_file(self, filepath: str) -> List[str]:
        category = self._get_namespace(filepath)
        rule_names = []

        try:
            with open(filepath, "r", encoding="utf-8") as f:
                content = f.read()

            current_rule = None
            current_desc = ""
            current_severity = "unknown"

            for line in content.split("\n"):
                line = line.strip()
                if line.startswith("rule "):
                    if current_rule:
                        rule_names.append(current_rule)
                        self._rules_metadata[current_rule] = YaraRule(
                            name=current_rule,
                            description=current_desc,
                            severity=current_severity,
                            category=category,
                        )
                    current_rule = line[5:].split(" ")[0].strip().strip(":")
                    current_desc = ""
                    current_severity = "unknown"
                elif line.startswith("description") and current_rule:
                    parts = line.split('"')
                    if len(parts) >= 2:
                        current_desc = parts[1]
                elif line.startswith("severity") and current_rule:
                    parts = line.split('"')
                    if len(parts) >= 2:
                        current_severity = parts[1]

            if current_rule:
                rule_names.append(current_rule)
                self._rules_metadata[current_rule] = YaraRule(
                    name=current_rule,
                    description=current_desc,
                    severity=current_severity,
                    category=category,
                )

        except Exception as e:
            error_msg = f"Error parsing rule file {filepath}: {e}"
            logger.error(error_msg)
            self._load_errors.append(error_msg)

        return rule_names

    def _compile_rules(
        self,
        filepaths: Dict[str, str],
    ) -> Optional[yara.Rules]:
        if not filepaths:
            return None

        try:
            compiled = yara.compile(filepaths=filepaths)
            return compiled
        except yara.SyntaxError as e:
            error_msg = f"YARA syntax error during compilation: {e}"
            logger.error(error_msg)
            self._load_errors.append(error_msg)
            return None
        except Exception as e:
            error_msg = f"Error compiling YARA rules: {e}"
            logger.error(error_msg)
            self._load_errors.append(error_msg)
            return None

    def _get_file_modified_time(self, filepath: str) -> float:
        try:
            return os.path.getmtime(filepath)
        except OSError:
            return 0.0

    def _get_changed_files(self, changed_files: Optional[Set[str]] = None) -> Dict[str, str]:
        current_files = self._collect_rule_files()

        if changed_files is None:
            return {self._get_namespace(f): f for f in current_files}

        result = {}
        for f in current_files:
            abs_path = os.path.abspath(f)
            namespace = self._get_namespace(f)

            if abs_path in changed_files:
                result[namespace] = f
                continue

            if namespace not in self._rule_files:
                result[namespace] = f
                continue

            info = self._rule_files[namespace]
            current_mtime = self._get_file_modified_time(f)
            if current_mtime > info.last_modified:
                result[namespace] = f

        return result

    def _remove_deleted_rules(self, current_files: List[str]) -> List[str]:
        current_namespaces = {self._get_namespace(f) for f in current_files}
        deleted_namespaces = set(self._rule_files.keys()) - current_namespaces

        deleted_rules = []
        for ns in deleted_namespaces:
            info = self._rule_files.get(ns)
            if info:
                deleted_rules.extend(self._get_rules_for_namespace(ns))
                del self._rule_files[ns]
                logger.info(f"Removed deleted rule file: {info.filepath}")

        return deleted_rules

    def _get_rules_for_namespace(self, namespace: str) -> List[str]:
        return [
            name for name, meta in self._rules_metadata.items()
            if meta.category == namespace
        ]

    def load_and_compile(self, force: bool = False) -> yara.Rules:
        with self._lock:
            if self._compiled_rules is not None and not force:
                return self._compiled_rules

            return self._reload_all()

    def _reload_all(self) -> yara.Rules:
        self._load_errors = []
        rule_files = self._collect_rule_files()

        if not rule_files:
            raise ValueError(f"No YARA rule files found in {self.rules_dir}")

        filepaths = {}
        for filepath in rule_files:
            namespace = self._get_namespace(filepath)
            filepaths[namespace] = filepath

        compiled = self._compile_rules(filepaths)
        if compiled is None:
            if self._compiled_rules is not None:
                logger.warning("Failed to compile rules, keeping previous version")
                return self._compiled_rules
            raise RuntimeError("Failed to compile YARA rules and no previous rules available")

        self._rules_metadata.clear()
        self._rule_files.clear()

        for filepath in rule_files:
            namespace = self._get_namespace(filepath)
            rule_names = self._parse_rule_file(filepath)
            mtime = self._get_file_modified_time(filepath)
            self._rule_files[namespace] = RuleFileInfo(
                filepath=filepath,
                namespace=namespace,
                last_modified=mtime,
                rules_count=len(rule_names),
            )

        self._compiled_rules = compiled
        self._last_reload_time = time.time()
        self._reload_count += 1

        logger.info(
            f"Loaded {len(rule_files)} rule files with "
            f"{len(self._rules_metadata)} rules"
        )
        return compiled

    def hot_reload(self, changed_files: Optional[Set[str]] = None) -> dict:
        with self._lock:
            self._load_errors = []
            start_time = time.time()

            current_files = self._collect_rule_files()

            if changed_files is not None:
                deleted_rules = self._remove_deleted_rules(current_files)
                if deleted_rules:
                    for rule_name in deleted_rules:
                        self._rules_metadata.pop(rule_name, None)

            filepaths_to_compile = {}
            for filepath in current_files:
                namespace = self._get_namespace(filepath)
                mtime = self._get_file_modified_time(filepath)

                needs_reload = False
                if namespace not in self._rule_files:
                    needs_reload = True
                elif mtime > self._rule_files[namespace].last_modified:
                    needs_reload = True

                if needs_reload or (changed_files is not None and os.path.abspath(filepath) in changed_files):
                    filepaths_to_compile[namespace] = filepath

            if changed_files is None:
                filepaths_to_compile = {
                    self._get_namespace(f): f for f in current_files
                }

            if not filepaths_to_compile and self._compiled_rules is not None:
                return {
                    "success": True,
                    "reloaded": False,
                    "message": "No changes detected",
                    "rules_count": len(self._rules_metadata),
                    "files_loaded": len(self._rule_files),
                    "reload_time_ms": 0,
                }

            existing_filepaths = {}
            for ns, info in self._rule_files.items():
                if ns not in filepaths_to_compile:
                    existing_filepaths[ns] = info.filepath

            all_filepaths = {**filepaths_to_compile, **existing_filepaths}

            compiled = self._compile_rules(all_filepaths)

            if compiled is None:
                return {
                    "success": False,
                    "reloaded": False,
                    "message": "Failed to compile rules, keeping previous version",
                    "errors": self._load_errors,
                    "rules_count": len(self._rules_metadata),
                }

            for filepath in filepaths_to_compile.values():
                namespace = self._get_namespace(filepath)
                old_rules = self._get_rules_for_namespace(namespace)
                for rule_name in old_rules:
                    self._rules_metadata.pop(rule_name, None)

            for filepath in filepaths_to_compile.values():
                namespace = self._get_namespace(filepath)
                rule_names = self._parse_rule_file(filepath)
                mtime = self._get_file_modified_time(filepath)
                self._rule_files[namespace] = RuleFileInfo(
                    filepath=filepath,
                    namespace=namespace,
                    last_modified=mtime,
                    rules_count=len(rule_names),
                )

            self._compiled_rules = compiled
            reload_time = (time.time() - start_time) * 1000
            self._last_reload_time = time.time()
            self._reload_count += 1

            result = {
                "success": True,
                "reloaded": True,
                "message": f"Successfully reloaded {len(filepaths_to_compile)} rule file(s)",
                "reloaded_files": list(filepaths_to_compile.keys()),
                "rules_count": len(self._rules_metadata),
                "files_loaded": len(self._rule_files),
                "reload_time_ms": round(reload_time, 2),
            }

            if self._load_errors:
                result["warnings"] = self._load_errors

            logger.info(
                f"Hot reload completed: {len(filepaths_to_compile)} file(s), "
                f"{len(self._rules_metadata)} rules, {reload_time:.2f}ms"
            )
            return result

    def get_compiled_rules(self) -> Optional[yara.Rules]:
        with self._lock:
            return self._compiled_rules

    def get_rule_metadata(self, rule_name: str) -> Optional[YaraRule]:
        return self._rules_metadata.get(rule_name)

    def get_all_rules_metadata(self) -> Dict[str, YaraRule]:
        return dict(self._rules_metadata)

    def scan_file(self, filepath: str) -> List[dict]:
        rules = self.get_compiled_rules()
        if rules is None:
            raise RuntimeError("No compiled rules available")

        matches = rules.match(filepath)
        return self._process_matches(matches)

    def scan_data(self, data: bytes) -> List[dict]:
        rules = self.get_compiled_rules()
        if rules is None:
            raise RuntimeError("No compiled rules available")

        matches = rules.match(data=data)
        return self._process_matches(matches)

    def _process_matches(self, matches: List) -> List[dict]:
        results = []
        for match in matches:
            rule_meta = self.get_rule_metadata(match.rule)
            for string_match in match.strings:
                for instance in string_match.instances:
                    result = {
                        "rule_name": match.rule,
                        "rule_description": rule_meta.description if rule_meta else "",
                        "severity": rule_meta.severity if rule_meta else "unknown",
                        "category": rule_meta.category if rule_meta else "unknown",
                        "offset": instance.offset,
                        "matched_string": str(string_match.data)[:100],
                    }
                    results.append(result)
        return results

    def get_rules_count(self) -> int:
        return len(self._rules_metadata)

    def get_status(self) -> dict:
        with self._lock:
            return {
                "rules_dir": self.rules_dir,
                "rules_count": len(self._rules_metadata),
                "files_loaded": len(self._rule_files),
                "reload_count": self._reload_count,
                "last_reload_time": self._last_reload_time,
                "has_compiled_rules": self._compiled_rules is not None,
                "recent_errors": self._load_errors[-10:] if self._load_errors else [],
                "rule_files": [
                    {
                        "namespace": info.namespace,
                        "filepath": info.filepath,
                        "rules_count": info.rules_count,
                        "last_modified": info.last_modified,
                    }
                    for info in self._rule_files.values()
                ],
            }
