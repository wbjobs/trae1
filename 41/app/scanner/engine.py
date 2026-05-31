import hashlib
import os
import logging
from typing import List, Optional, Set, Dict, Any
from app.scanner.yara_rules import YaraRuleLoader
from app.scanner.file_watcher import RuleFileWatcher
from app.scanner.heuristic import HeuristicScanner
from app.db.models import SessionLocal
from app.db.crud import ScanResultCRUD
from app.config import settings

logger = logging.getLogger(__name__)

SEVERITY_ORDER = {
    "clean": 0,
    "low": 1,
    "medium": 2,
    "high": 3,
    "critical": 4,
}


class ScanEngine:
    def __init__(self, enable_hot_reload: bool = True, enable_heuristic: bool = True):
        self.rule_loader = YaraRuleLoader(settings.yara_rules_dir)
        self.rule_loader.load_and_compile()

        self.heuristic_scanner = HeuristicScanner() if enable_heuristic else None
        self._heuristic_enabled = enable_heuristic

        self._watcher: Optional[RuleFileWatcher] = None
        self._hot_reload_enabled = enable_hot_reload

        if enable_hot_reload:
            self._init_file_watcher()

    def _init_file_watcher(self):
        try:
            self._watcher = RuleFileWatcher(
                rules_dir=settings.yara_rules_dir,
                on_change_callback=self._on_rules_changed,
                debounce_seconds=2.0,
            )
            success = self._watcher.start()
            if not success:
                logger.warning("Failed to start rule file watcher, hot reload disabled")
                self._hot_reload_enabled = False
        except Exception as e:
            logger.error(f"Error initializing file watcher: {e}")
            self._hot_reload_enabled = False

    def _on_rules_changed(self, changed_files: Set[str]):
        logger.info(f"Rules changed, triggering hot reload for: {changed_files}")
        try:
            result = self.rule_loader.hot_reload(changed_files)
            if result.get("success") and result.get("reloaded"):
                logger.info(
                    f"Rules hot reloaded: {result.get('rules_count')} rules, "
                    f"{result.get('reload_time_ms')}ms"
                )
            elif result.get("success") and not result.get("reloaded"):
                logger.debug("No rule changes required reload")
            else:
                logger.error(f"Rule hot reload failed: {result.get('message')}")
        except Exception as e:
            logger.error(f"Error during rule hot reload: {e}")

    def calculate_md5(self, data: bytes) -> str:
        return hashlib.md5(data).hexdigest()

    def _determine_severity(
        self, yara_matches: List[dict], heuristic_result: Optional[Dict] = None
    ) -> str:
        severities = []

        for match in yara_matches:
            severities.append(match.get("severity", "low"))

        if heuristic_result and heuristic_result.get("findings"):
            for finding in heuristic_result["findings"]:
                severities.append(finding.get("severity", "low"))

        if not severities:
            return "clean"

        highest = "low"
        for sev in severities:
            if SEVERITY_ORDER.get(sev, 0) > SEVERITY_ORDER.get(highest, 0):
                highest = sev

        return highest

    def _get_cached_result(self, file_md5: str) -> Optional[dict]:
        db = SessionLocal()
        try:
            crud = ScanResultCRUD(db)
            result = crud.get_by_md5(file_md5)
            if result:
                return {
                    "cached": True,
                    "file_md5": file_md5,
                    "file_name": result.file_name,
                    "file_size": result.file_size,
                    "matched_rules": result.to_dict()["matched_rules"],
                    "scan_time": result.scan_time.isoformat(),
                    "total_matches": result.total_matches,
                    "highest_severity": result.highest_severity,
                    "heuristic_scan": result.to_dict().get("heuristic_scan"),
                }
            return None
        finally:
            db.close()

    def _save_scan_result(
        self,
        file_md5: str,
        file_name: str,
        file_size: int,
        matched_rules: List[dict],
        heuristic_result: Optional[Dict] = None,
    ) -> dict:
        db = SessionLocal()
        try:
            crud = ScanResultCRUD(db)
            existing = crud.get_by_md5(file_md5)
            highest_severity = self._determine_severity(matched_rules, heuristic_result)
            total_matches = len(matched_rules)

            scan_data = list(matched_rules)
            if heuristic_result and heuristic_result.get("findings"):
                for finding in heuristic_result["findings"]:
                    scan_data.append({
                        "rule_name": f"heuristic:{finding['name']}",
                        "rule_description": finding["description"],
                        "severity": finding["severity"],
                        "category": finding["type"],
                        "offset": finding.get("offset"),
                        "matched_string": finding.get("details", "")[:100],
                    })
                total_matches += len(heuristic_result["findings"])

            if existing:
                result = crud.update(
                    file_md5=file_md5,
                    matched_rules=scan_data,
                    total_matches=total_matches,
                    highest_severity=highest_severity,
                )
            else:
                result = crud.create(
                    file_md5=file_md5,
                    file_name=file_name,
                    file_size=file_size,
                    matched_rules=scan_data,
                    total_matches=total_matches,
                    highest_severity=highest_severity,
                )

            return {
                "cached": False,
                "file_md5": file_md5,
                "file_name": file_name,
                "file_size": file_size,
                "matched_rules": scan_data,
                "scan_time": result.scan_time.isoformat(),
                "total_matches": total_matches,
                "highest_severity": highest_severity,
                "heuristic_scan": heuristic_result,
            }
        finally:
            db.close()

    def _run_heuristic_scan(
        self, file_name: str, file_data: bytes, yara_matches: List[dict]
    ) -> Optional[Dict]:
        if not self.heuristic_scanner:
            return None

        should_run_heuristic = False
        if not yara_matches:
            should_run_heuristic = True
        else:
            yara_severities = {m.get("severity", "low") for m in yara_matches}
            if not any(s in ("high", "critical") for s in yara_severities):
                should_run_heuristic = True

        if not should_run_heuristic:
            return None

        try:
            result = self.heuristic_scanner.scan(file_data, file_name)
            if result.get("findings"):
                logger.info(
                    f"Heuristic scan found {result['total_findings']} "
                    f"findings for {file_name} (entropy: {result['entropy']})"
                )
            return result
        except Exception as e:
            logger.error(f"Heuristic scan error for {file_name}: {e}")
            return {
                "enabled": True,
                "findings": [],
                "total_findings": 0,
                "highest_severity": "clean",
                "entropy": 0,
                "entropy_category": "unknown",
            }

    def scan_file(self, file_name: str, file_data: bytes) -> dict:
        file_size = len(file_data)
        file_md5 = self.calculate_md5(file_data)

        cached = self._get_cached_result(file_md5)
        if cached:
            return cached

        yara_matches = self.rule_loader.scan_data(file_data)

        heuristic_result = self._run_heuristic_scan(file_name, file_data, yara_matches)

        return self._save_scan_result(
            file_md5=file_md5,
            file_name=file_name,
            file_size=file_size,
            matched_rules=yara_matches,
            heuristic_result=heuristic_result,
        )

    def scan_files(self, files: List[tuple[str, bytes]]) -> List[dict]:
        results = []
        for file_name, file_data in files:
            try:
                result = self.scan_file(file_name, file_data)
                results.append(result)
            except Exception as e:
                results.append({
                    "error": str(e),
                    "file_name": file_name,
                    "cached": False,
                })
        return results

    def get_scan_history(
        self,
        skip: int = 0,
        limit: int = 100,
        severity: Optional[str] = None,
    ) -> List[dict]:
        db = SessionLocal()
        try:
            crud = ScanResultCRUD(db)
            results = crud.list_results(skip=skip, limit=limit, severity=severity)
            return [r.to_dict() for r in results]
        finally:
            db.close()

    def get_scan_stats(self) -> dict:
        db = SessionLocal()
        try:
            crud = ScanResultCRUD(db)
            total = crud.count()
            stats = {
                "total_scans": total,
                "rules_count": self.rule_loader.get_rules_count(),
                "heuristic_enabled": self._heuristic_enabled,
            }
            for severity in ["clean", "low", "medium", "high", "critical"]:
                stats[f"{severity}_count"] = crud.count(severity=severity)
            return stats
        finally:
            db.close()

    def get_result_by_md5(self, file_md5: str) -> Optional[dict]:
        cached = self._get_cached_result(file_md5)
        return cached

    def reload_rules(self, force: bool = False) -> dict:
        if force:
            return self.rule_loader.hot_reload(None)
        return self.rule_loader.hot_reload()

    def get_rules_status(self) -> dict:
        status = self.rule_loader.get_status()
        status["hot_reload_enabled"] = self._hot_reload_enabled
        status["heuristic_scan_enabled"] = self._heuristic_enabled
        if self._watcher:
            status["watcher"] = self._watcher.get_status()
        return status

    def shutdown(self):
        if self._watcher:
            self._watcher.stop()
            self._watcher = None
