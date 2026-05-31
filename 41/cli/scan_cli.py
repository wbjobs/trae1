#!/usr/bin/env python3
import argparse
import sys
import os
import json
from typing import List, Optional

try:
    import httpx
except ImportError:
    print("Error: httpx is required. Install it with: pip install httpx")
    sys.exit(1)


class MalwareScannerCLI:
    def __init__(self, base_url: str = "http://localhost:8000"):
        self.base_url = base_url.rstrip("/")
        self.client = httpx.Client(timeout=120.0)

    def close(self):
        self.client.close()

    def scan_file(self, filepath: str) -> dict:
        if not os.path.exists(filepath):
            return {"error": f"File not found: {filepath}"}

        file_size = os.path.getsize(filepath)
        max_size = 50 * 1024 * 1024
        if file_size > max_size:
            return {"error": f"File too large ({file_size / 1024 / 1024:.2f}MB > 50MB)"}

        filename = os.path.basename(filepath)
        try:
            with open(filepath, "rb") as f:
                files = {"file": (filename, f, "application/octet-stream")}
                response = self.client.post(
                    f"{self.base_url}/api/v1/scan",
                    files=files,
                )
                response.raise_for_status()
                return response.json()
        except httpx.ConnectError:
            return {"error": f"Cannot connect to server at {self.base_url}"}
        except httpx.HTTPError as e:
            return {"error": f"HTTP error: {e}"}

    def scan_files(self, filepaths: List[str]) -> dict:
        existing_files = [(fp, os.path.basename(fp)) for fp in filepaths if os.path.exists(fp)]
        if not existing_files:
            return {"error": "No valid files found"}

        files_to_upload = []
        for filepath, filename in existing_files:
            file_size = os.path.getsize(filepath)
            if file_size <= 50 * 1024 * 1024:
                files_to_upload.append(("files", (filename, open(filepath, "rb"), "application/octet-stream")))

        if not files_to_upload:
            return {"error": "All files exceed 50MB limit"}

        try:
            response = self.client.post(
                f"{self.base_url}/api/v1/scan/batch",
                files=files_to_upload,
            )
            response.raise_for_status()
            return response.json()
        except httpx.ConnectError:
            return {"error": f"Cannot connect to server at {self.base_url}"}
        except httpx.HTTPError as e:
            return {"error": f"HTTP error: {e}"}
        finally:
            for _, (_, f, _) in files_to_upload:
                f.close()

    def get_result(self, md5: str) -> dict:
        try:
            response = self.client.get(f"{self.base_url}/api/v1/scan/result/{md5}")
            response.raise_for_status()
            return response.json()
        except httpx.ConnectError:
            return {"error": f"Cannot connect to server at {self.base_url}"}
        except httpx.HTTPStatusError as e:
            if e.response.status_code == 404:
                return {"error": "Result not found"}
            return {"error": f"HTTP error: {e}"}

    def get_history(self, limit: int = 50, severity: Optional[str] = None) -> dict:
        params = {"limit": limit}
        if severity:
            params["severity"] = severity
        try:
            response = self.client.get(
                f"{self.base_url}/api/v1/scan/history",
                params=params,
            )
            response.raise_for_status()
            return response.json()
        except httpx.ConnectError:
            return {"error": f"Cannot connect to server at {self.base_url}"}

    def get_stats(self) -> dict:
        try:
            response = self.client.get(f"{self.base_url}/api/v1/stats")
            response.raise_for_status()
            return response.json()
        except httpx.ConnectError:
            return {"error": f"Cannot connect to server at {self.base_url}"}

    def health_check(self) -> dict:
        try:
            response = self.client.get(f"{self.base_url}/api/v1/health")
            response.raise_for_status()
            return response.json()
        except httpx.ConnectError:
            return {"error": f"Cannot connect to server at {self.base_url}"}

    def reload_rules(self, force: bool = False) -> dict:
        try:
            response = self.client.post(
                f"{self.base_url}/api/v1/rules/reload",
                json={"force": force},
            )
            response.raise_for_status()
            return response.json()
        except httpx.ConnectError:
            return {"error": f"Cannot connect to server at {self.base_url}"}
        except httpx.HTTPError as e:
            return {"error": f"HTTP error: {e}"}

    def get_rules_status(self) -> dict:
        try:
            response = self.client.get(f"{self.base_url}/api/v1/rules/status")
            response.raise_for_status()
            return response.json()
        except httpx.ConnectError:
            return {"error": f"Cannot connect to server at {self.base_url}"}
        except httpx.HTTPError as e:
            return {"error": f"HTTP error: {e}"}

    def list_rules(self, category: Optional[str] = None, severity: Optional[str] = None) -> dict:
        params = {}
        if category:
            params["category"] = category
        if severity:
            params["severity"] = severity
        try:
            response = self.client.get(
                f"{self.base_url}/api/v1/rules/list",
                params=params,
            )
            response.raise_for_status()
            return response.json()
        except httpx.ConnectError:
            return {"error": f"Cannot connect to server at {self.base_url}"}
        except httpx.HTTPError as e:
            return {"error": f"HTTP error: {e}"}


def print_scan_result(result: dict):
    if "error" in result:
        print(f"\n  [ERROR] {result['error']}")
        return

    print(f"\n  File: {result.get('file_name', 'N/A')}")
    print(f"  MD5: {result.get('file_md5', 'N/A')}")
    print(f"  Size: {result.get('file_size', 'N/A')} bytes")
    print(f"  Scan Time: {result.get('scan_time', 'N/A')}")
    print(f"  Cached: {'Yes' if result.get('cached') else 'No'}")
    print(f"  Severity: {result.get('highest_severity', 'clean').upper()}")
    print(f"  Total Matches: {result.get('total_matches', 0)}")

    rules = result.get("matched_rules", [])
    yara_rules = [r for r in rules if not r.get("rule_name", "").startswith("heuristic:")]
    heuristic_rules = [r for r in rules if r.get("rule_name", "").startswith("heuristic:")]

    if yara_rules:
        print(f"\n  [YARA MATCHED RULES]")
        for i, rule in enumerate(yara_rules, 1):
            print(f"    {i}. {rule.get('rule_name', 'Unknown')}")
            if rule.get("rule_description"):
                print(f"       Description: {rule['rule_description']}")
            print(f"       Severity: {rule.get('severity', 'unknown')}")
            print(f"       Offset: {rule.get('offset', 'N/A')}")
            print(f"       Category: {rule.get('category', 'unknown')}")

    if heuristic_rules:
        print(f"\n  [HEURISTIC FINDINGS]")
        for i, rule in enumerate(heuristic_rules, 1):
            print(f"    {i}. {rule.get('rule_name', 'Unknown').replace('heuristic:', '')}")
            if rule.get("rule_description"):
                print(f"       Description: {rule['rule_description']}")
            print(f"       Severity: {rule.get('severity', 'unknown')}")
            if rule.get("offset") is not None:
                print(f"       Offset: {rule['offset']}")
            print(f"       Type: {rule.get('category', 'unknown')}")

    heuristic_scan = result.get("heuristic_scan")
    if heuristic_scan:
        print(f"\n  [HEURISTIC ANALYSIS]")
        print(f"    Entropy: {heuristic_scan.get('entropy', 0):.4f} ({heuristic_scan.get('entropy_category', 'unknown')})")
        print(f"    Findings: {heuristic_scan.get('total_findings', 0)}")
        findings = heuristic_scan.get("findings", [])
        for i, finding in enumerate(findings, 1):
            print(f"      {i}. [{finding.get('severity', 'unknown').upper()}] {finding.get('name', 'Unknown')}")
            if finding.get("description"):
                print(f"         {finding['description']}")
            if finding.get("details"):
                print(f"         Details: {finding['details'][:80]}")

    if not yara_rules and not heuristic_rules:
        print(f"\n  [CLEAN] No malicious patterns detected")


def print_batch_results(result: dict):
    if "error" in result:
        print(f"\n  [ERROR] {result['error']}")
        return

    print(f"\n  [BATCH SCAN RESULTS]")
    print(f"  Total Files: {result.get('total_files', 0)}")
    print(f"  Scanned: {result.get('scanned_count', 0)}")
    print(f"  Cached: {result.get('cached_count', 0)}")
    print(f"  Malicious: {result.get('malicious_count', 0)}")

    for i, item in enumerate(result.get("results", []), 1):
        print(f"\n  --- File {i} ---")
        print_scan_result(item)


def print_history(result: dict):
    if "error" in result:
        print(f"\n  [ERROR] {result['error']}")
        return

    print(f"\n  [SCAN HISTORY]")
    results = result.get("results", [])
    print(f"  Total records: {len(results)}")

    for i, item in enumerate(results, 1):
        print(f"\n  {i}. {item.get('file_name', 'N/A')}")
        print(f"     MD5: {item.get('file_md5', 'N/A')}")
        print(f"     Severity: {item.get('highest_severity', 'clean').upper()}")
        print(f"     Matches: {item.get('total_matches', 0)}")
        print(f"     Time: {item.get('scan_time', 'N/A')}")


def print_stats(result: dict):
    if "error" in result:
        print(f"\n  [ERROR] {result['error']}")
        return

    print(f"\n  [SCANNER STATISTICS]")
    print(f"  Total Scans: {result.get('total_scans', 0)}")
    print(f"  Rules Loaded: {result.get('rules_count', 0)}")
    for key in ["clean", "low", "medium", "high", "critical"]:
        count = result.get(f"{key}_count", 0)
        print(f"  {key.upper()}: {count}")


def print_rules_status(result: dict):
    if "error" in result:
        print(f"\n  [ERROR] {result['error']}")
        return

    print(f"\n  [RULES STATUS]")
    print(f"  Rules Directory: {result.get('rules_dir', 'N/A')}")
    print(f"  Total Rules: {result.get('rules_count', 0)}")
    print(f"  Files Loaded: {result.get('files_loaded', 0)}")
    print(f"  Hot Reload: {'Enabled' if result.get('hot_reload_enabled') else 'Disabled'}")
    print(f"  Reload Count: {result.get('reload_count', 0)}")
    print(f"  Last Reload: {result.get('last_reload_time', 'N/A')}")

    if result.get("watcher"):
        watcher = result["watcher"]
        print(f"  File Watcher: {'Running' if watcher.get('running') else 'Stopped'}")
        print(f"  Watch Directory: {watcher.get('rules_dir', 'N/A')}")
        print(f"  Debounce: {watcher.get('debounce_seconds', 0)}s")

    rule_files = result.get("rule_files", [])
    if rule_files:
        print(f"\n  [RULE FILES]")
        for rf in rule_files:
            print(f"    - {rf.get('namespace', 'N/A')}: {rf.get('rules_count', 0)} rules")


def print_reload_result(result: dict):
    if "error" in result:
        print(f"\n  [ERROR] {result['error']}")
        return

    print(f"\n  [RULES RELOAD]")
    print(f"  Success: {'Yes' if result.get('success') else 'No'}")
    print(f"  Reloaded: {'Yes' if result.get('reloaded') else 'No'}")
    print(f"  Message: {result.get('message', 'N/A')}")

    if result.get("reloaded_files"):
        print(f"  Reloaded Files: {', '.join(result['reloaded_files'])}")
    print(f"  Total Rules: {result.get('rules_count', 0)}")
    print(f"  Files Loaded: {result.get('files_loaded', 0)}")

    if result.get("reload_time_ms"):
        print(f"  Reload Time: {result['reload_time_ms']}ms")

    if result.get("warnings"):
        print(f"\n  [WARNINGS]")
        for w in result["warnings"]:
            print(f"    - {w}")

    if result.get("errors"):
        print(f"\n  [ERRORS]")
        for e in result["errors"]:
            print(f"    - {e}")


def print_rules_list(result: dict):
    if "error" in result:
        print(f"\n  [ERROR] {result['error']}")
        return

    rules = result.get("rules", [])
    print(f"\n  [RULES LIST]")
    print(f"  Total: {result.get('total', 0)}")

    current_category = None
    for rule in rules:
        cat = rule.get("category", "unknown")
        if cat != current_category:
            current_category = cat
            print(f"\n  [{cat.upper()}]")
        sev = rule.get("severity", "unknown")
        name = rule.get("name", "Unknown")
        desc = rule.get("description", "")
        print(f"    - [{sev.upper():8s}] {name}")
        if desc:
            print(f"      {desc}")


def main():
    parser = argparse.ArgumentParser(
        description="Malware Scanner CLI - Upload files for YARA-based scanning",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s scan file.exe
  %(prog)s scan file1.exe file2.exe file3.exe
  %(prog)s lookup <md5>
  %(prog)s history --limit 20
  %(prog)s stats
  %(prog)s health
  %(prog)s rules reload [--force]
  %(prog)s rules status
  %(prog)s rules list [--category malware] [--severity high]
        """,
    )

    parser.add_argument(
        "--url",
        default="http://localhost:8000",
        help="Scanner service URL (default: http://localhost:8000)",
    )

    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    scan_parser = subparsers.add_parser("scan", help="Scan file(s)")
    scan_parser.add_argument("files", nargs="+", help="File(s) to scan")

    lookup_parser = subparsers.add_parser("lookup", help="Lookup scan result by MD5")
    lookup_parser.add_argument("md5", help="MD5 hash of the file")

    history_parser = subparsers.add_parser("history", help="View scan history")
    history_parser.add_argument("--limit", type=int, default=50, help="Number of records")
    history_parser.add_argument(
        "--severity",
        choices=["clean", "low", "medium", "high", "critical"],
        help="Filter by severity",
    )

    subparsers.add_parser("stats", help="View scanner statistics")
    subparsers.add_parser("health", help="Health check")

    rules_parser = subparsers.add_parser("rules", help="Rules management (hot reload)")
    rules_sub = rules_parser.add_subparsers(dest="rules_command", help="Rules commands")

    reload_parser = rules_sub.add_parser("reload", help="Reload YARA rules (hot reload)")
    reload_parser.add_argument("--force", action="store_true", help="Force full reload")

    rules_sub.add_parser("status", help="Show rules status")

    list_parser = rules_sub.add_parser("list", help="List all loaded rules")
    list_parser.add_argument("--category", help="Filter by category (malware, trojan, etc.)")
    list_parser.add_argument(
        "--severity",
        choices=["clean", "low", "medium", "high", "critical"],
        help="Filter by severity",
    )

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    cli = MalwareScannerCLI(base_url=args.url)

    try:
        if args.command == "scan":
            if len(args.files) == 1:
                result = cli.scan_file(args.files[0])
                print("\n" + "=" * 60)
                print("  MALWARE SCAN RESULT")
                print("=" * 60)
                print_scan_result(result)
                print("=" * 60 + "\n")
            else:
                result = cli.scan_files(args.files)
                print("\n" + "=" * 60)
                print("  BATCH MALWARE SCAN RESULTS")
                print("=" * 60)
                print_batch_results(result)
                print("=" * 60 + "\n")

        elif args.command == "lookup":
            result = cli.get_result(args.md5)
            print("\n" + "=" * 60)
            print("  SCAN RESULT LOOKUP")
            print("=" * 60)
            print_scan_result(result)
            print("=" * 60 + "\n")

        elif args.command == "history":
            result = cli.get_history(limit=args.limit, severity=args.severity)
            print("\n" + "=" * 60)
            print("  SCAN HISTORY")
            print("=" * 60)
            print_history(result)
            print("=" * 60 + "\n")

        elif args.command == "stats":
            result = cli.get_stats()
            print("\n" + "=" * 60)
            print("  SCANNER STATISTICS")
            print("=" * 60)
            print_stats(result)
            print("=" * 60 + "\n")

        elif args.command == "health":
            result = cli.health_check()
            print("\n" + "=" * 60)
            print("  HEALTH CHECK")
            print("=" * 60)
            if "error" in result:
                print(f"\n  [ERROR] {result['error']}")
            else:
                print(f"\n  Status: {result.get('status', 'unknown')}")
                print(f"  Rules Loaded: {result.get('rules_loaded', 0)}")
                hot_reload = result.get("hot_reload_enabled", False)
                print(f"  Hot Reload: {'Enabled' if hot_reload else 'Disabled'}")
                heuristic = result.get("heuristic_scan_enabled", False)
                print(f"  Heuristic Scan: {'Enabled' if heuristic else 'Disabled'}")
            print("=" * 60 + "\n")

        elif args.command == "rules":
            if not args.rules_command:
                rules_parser.print_help()
                sys.exit(1)

            if args.rules_command == "reload":
                result = cli.reload_rules(force=args.force)
                print("\n" + "=" * 60)
                print("  RULES HOT RELOAD")
                print("=" * 60)
                print_reload_result(result)
                print("=" * 60 + "\n")

            elif args.rules_command == "status":
                result = cli.get_rules_status()
                print("\n" + "=" * 60)
                print("  RULES STATUS")
                print("=" * 60)
                print_rules_status(result)
                print("=" * 60 + "\n")

            elif args.rules_command == "list":
                result = cli.list_rules(category=args.category, severity=args.severity)
                print("\n" + "=" * 60)
                print("  RULES LIST")
                print("=" * 60)
                print_rules_list(result)
                print("=" * 60 + "\n")

    finally:
        cli.close()


if __name__ == "__main__":
    main()
