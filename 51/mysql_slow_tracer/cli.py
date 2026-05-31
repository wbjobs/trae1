"""Command-line interface for MySQL Slow Query Tracer.

Supports MySQL 5.7 and 8.0 with auto-detection, manual offset overrides,
version-aware symbol resolution, and optional AI-powered optimization
suggestions via local Ollama (qwen2.5-coder:7b).

Usage:
    python -m mysql_slow_tracer.cli --pid <PID> [options]
    python -m mysql_slow_tracer.cli --pid <PID> --detect
    python -m mysql_slow_tracer.cli --pid <PID> --ai
    python -m mysql_slow_tracer.cli --pid <PID> --func-alias dispatch_command=actual_name
"""

import argparse
import logging
import os
import signal
import sys
import time
from typing import Dict, List, Optional

from .config import Config
from .symbols import MySQLSymbolResolver
from .tracer import TracerEventType, BCC_AVAILABLE
from .collector import DataCollector, QueryEvent
from .report import ReportGenerator
from .version import (
    MySQLVersionDetector,
    FunctionMapper,
    CRITICAL_FUNCTIONS,
)
from .ai_analyzer import AIAnalyzer, AISuggestion, OllamaClient

logger = logging.getLogger(__name__)


def setup_logging(verbose: bool = False):
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )


def validate_pid(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def detect_mysql_binary(pid: int) -> Optional[str]:
    try:
        link = os.readlink(f"/proc/{pid}/exe")
        return link
    except (OSError, PermissionError):
        pass

    try:
        with open(f"/proc/{pid}/cmdline", "rb") as f:
            cmdline = f.read().decode("utf-8", errors="replace")
            binary = cmdline.split("\x00")[0]
            if binary and os.path.exists(binary):
                return binary
    except (OSError, PermissionError):
        pass

    return None


def parse_func_alias(args_list: List[str]) -> Dict[str, str]:
    result = {}
    if not args_list:
        return result
    for arg in args_list:
        if "=" not in arg:
            logger.warning(f"Invalid --func-alias format: '{arg}'. Expected: logical_name=symbol_name")
            continue
        key, value = arg.split("=", 1)
        result[key.strip()] = value.strip()
    return result


def parse_func_offset(args_list: List[str]) -> Dict[str, int]:
    result = {}
    if not args_list:
        return result
    for arg in args_list:
        if "=" not in arg:
            logger.warning(f"Invalid --func-offset format: '{arg}'. Expected: logical_name=0xHEX")
            continue
        key, value = arg.split("=", 1)
        key = key.strip()
        value = value.strip()
        try:
            if value.startswith("0x") or value.startswith("0X"):
                result[key] = int(value, 16)
            else:
                result[key] = int(value)
        except ValueError:
            logger.warning(f"Invalid offset value for '{key}': {value}")
    return result


def cmd_detect(config: Config, symbol_resolver: MySQLSymbolResolver):
    """Run symbol detection and print a comprehensive report."""
    logger.info("Running MySQL symbol detection...")
    print("=" * 80)

    version_detector = MySQLVersionDetector(config.mysql_binary, config.pid)
    version = version_detector.detect()

    print(f"  Binary:         {config.mysql_binary}")
    print(f"  PID:            {config.pid}")
    print(f"  MySQL Version:  {version.major}.{version.minor}")
    print(f"  Version Full:   {version.full}")
    print(f"  Detected Via:   {version.source}")
    print(f"  Total Symbols:  {len(symbol_resolver._symbols)}")
    print(f"  Demangled:      {len(symbol_resolver._demangled)}")
    print()

    func_mapper = FunctionMapper(
        version_detector,
        symbol_resolver,
        func_aliases=config.func_aliases,
        func_offsets=config.func_offsets,
    )

    all_funcs = list(dict.fromkeys(
        ["dispatch_command"]
        + config.mysql_funcs
        + config.io_funcs
        + config.lock_funcs
    ))

    print(f"  {'Function':<35} {'Status':<8} {'Resolved Symbol / Offset':<50}")
    print(f"  {'-'*33} {'-'*6} {'-'*48}")

    unresolved = []
    for name in all_funcs:
        mapping = func_mapper.resolve(name)
        status = "OK" if mapping.has_resolved() else "MISSING"
        if mapping.offset > 0:
            detail = f"offset={hex(mapping.offset)}"
        elif mapping.resolved_symbol:
            sym = mapping.resolved_symbol
            if len(sym) > 47:
                sym = sym[:44] + "..."
            detail = sym
        else:
            detail = "(not found)"
            unresolved.append(name)

        print(f"  {name:<35} {status:<8} {detail:<50}")

    print()

    if unresolved:
        print(f"  UNRESOLVED FUNCTIONS ({len(unresolved)}):")
        for name in unresolved:
            mapping = func_mapper.resolve(name)
            print(f"    - {name}")
            if mapping.available_symbols:
                for cand in mapping.available_symbols[:3]:
                    print(f"        candidate: {cand}")
        print()
        print("  To resolve missing functions:")
        print("    1. Install debug symbols: apt install mysql-server-dbg")
        print("    2. Use --func-offset to specify hex offsets:")
        print("       --func-offset dispatch_command=0x123456")
        print("    3. Use --func-alias to map logical names to actual symbols:")
        print("       --func-alias dispatch_command=_Z16dispatch_commandP3THDPK8COM_DATA19enum_server_command")
    else:
        print("  All functions resolved successfully!")
        print()
        print("  Config JSON for reference:")
        print(config.to_json())

    print("=" * 80)

    config_path = os.path.join(config.output_dir, "detected_config.json")
    config.save(config_path)
    print(f"  Config saved to: {config_path}")

    return unresolved


def _run_ai_analysis(
    ai_analyzer: AIAnalyzer,
    queries: List[QueryEvent],
) -> int:
    """Run AI analysis on all captured queries, print to terminal, and attach results."""
    if not ai_analyzer or not ai_analyzer.enabled:
        return 0

    total = len(queries)
    analyzed = 0

    logger.info(
        f"Starting AI analysis for {total} queries "
        f"(model: {ai_analyzer.client.model})..."
    )

    for i, qe in enumerate(queries):
        label = f"[{i + 1}/{total}]"
        logger.info(
            f"{label} Analyzing: {qe.sql[:70]}{'...' if len(qe.sql) > 70 else ''}"
        )

        suggestion = ai_analyzer.analyze(qe)
        qe.ai_suggestion = suggestion.to_dict()

        if suggestion.has_content():
            analyzed += 1
            terminal_output = ai_analyzer.format_terminal(suggestion)
            print(terminal_output)
        elif suggestion.error:
            logger.warning(f"  AI analysis error: {suggestion.error}")
        else:
            logger.info("  No actionable suggestions generated.")

    return analyzed


def run_tracer(config: Config, ai_analyzer: Optional[AIAnalyzer] = None):
    """Main tracing loop with optional AI analysis."""
    logger.info("Starting MySQL Slow Query Tracer...")
    logger.info(f"Target PID: {config.pid}")
    logger.info(f"Threshold: {config.threshold_ms}ms")
    logger.info(f"Duration: {config.duration}s")
    logger.info(f"MySQL binary: {config.mysql_binary}")

    if ai_analyzer and ai_analyzer.enabled:
        logger.info(f"AI analysis: enabled (model: {ai_analyzer.client.model})")

    if not os.path.exists(config.mysql_binary):
        logger.error(f"MySQL binary not found: {config.mysql_binary}")
        sys.exit(1)

    symbol_resolver = MySQLSymbolResolver(config.mysql_binary)
    logger.info("Loading MySQL symbols...")
    symbol_resolver.load_symbols()

    version_detector = MySQLVersionDetector(config.mysql_binary, config.pid)
    version = version_detector.detect()
    logger.info(f"Detected MySQL {version.major} (via {version.source})")

    func_mapper = FunctionMapper(
        version_detector,
        symbol_resolver,
        func_aliases=config.func_aliases,
        func_offsets=config.func_offsets,
    )

    dispatch_mapping = func_mapper.resolve("dispatch_command")
    if not dispatch_mapping.has_resolved():
        logger.error(
            "Cannot find 'dispatch_command' in MySQL symbols.\n"
            "The binary may be stripped or not contain debug symbols.\n"
            "Options:\n"
            "  1. Install debug symbols: apt install mysql-server-dbg\n"
            "  2. Run --detect to see available symbols\n"
            "  3. Specify offset: --func-offset dispatch_command=0x123456\n"
            "  4. Specify alias:  --func-alias dispatch_command=actual_name\n"
        )
        logger.info(
            "Available dispatch-related symbols:\n"
            + symbol_resolver.get_symbol_table_summary()
        )
        sys.exit(1)

    logger.info(
        f"Resolved dispatch_command -> {dispatch_mapping.resolved_symbol or hex(dispatch_mapping.offset)}"
    )

    tracer = TracerEventType(
        config,
        symbol_resolver,
        func_mapper=func_mapper,
        version_detector=version_detector,
    )
    collector = DataCollector(max_events=config.max_events)
    report_gen = ReportGenerator(output_dir=config.output_dir)

    def on_event(event):
        qe = collector.handle_event(event)
        if qe:
            logger.info(
                f"[{len(collector.get_completed_queries())}] "
                f"Slow query detected: {qe.duration_ms:.1f}ms "
                f"(I/O: {qe.io_wait_ms:.1f}ms, Lock: {qe.lock_wait_ms:.1f}ms)"
            )

    tracer.register_callback(on_event)

    try:
        tracer.start()
    except RuntimeError as e:
        logger.error(f"Failed to start tracer: {e}")
        sys.exit(1)
    except Exception as e:
        logger.error(f"Unexpected error starting tracer: {e}")
        sys.exit(1)

    running = True

    def handle_sigint(signum, frame):
        nonlocal running
        logger.info("Interrupt received, stopping...")
        running = False

    signal.signal(signal.SIGINT, handle_sigint)
    signal.signal(signal.SIGTERM, handle_sigint)

    start_time = time.time()
    last_stats_time = start_time
    poll_interval = 0.05

    logger.info(f"Tracing for {config.duration} seconds (Ctrl+C to stop early)...")

    try:
        while running:
            elapsed = time.time() - start_time

            if elapsed >= config.duration:
                logger.info(f"Duration limit reached ({config.duration}s)")
                break

            tracer.poll(timeout_ms=50)

            if time.time() - last_stats_time > 5:
                stats = collector.get_summary()
                queries = collector.get_completed_queries()
                logger.info(
                    f"[{elapsed:.0f}s] Queries: {stats.get('total_queries', 0)} | "
                    f"Events: {stats.get('total_events', 0)} | "
                    f"Avg: {stats.get('avg_duration_ms', 0):.1f}ms | "
                    f"Max: {stats.get('max_duration_ms', 0):.1f}ms"
                )
                last_stats_time = time.time()

            time.sleep(poll_interval)

    except KeyboardInterrupt:
        logger.info("Interrupted by user")

    finally:
        tracer.stop()
        logger.info("Tracer stopped. Generating report...")

    queries = collector.get_completed_queries()

    if not queries:
        logger.warning("No slow queries captured. Consider lowering the threshold.")
        return

    ai_queries_analyzed = 0
    if ai_analyzer and ai_analyzer.enabled:
        ai_queries_analyzed = _run_ai_analysis(ai_analyzer, queries)

    logger.info(f"Generating report for {len(queries)} slow queries...")

    html_path = report_gen.generate_report(
        queries=queries,
        pid=config.pid,
        threshold_ms=config.threshold_ms,
        duration=int(time.time() - start_time),
        tracer=tracer,
        ai_enabled=(ai_analyzer is not None and ai_analyzer.enabled),
        ai_model=(ai_analyzer.client.model if ai_analyzer and ai_analyzer.enabled else ""),
        ai_queries_analyzed=ai_queries_analyzed,
    )

    json_path = report_gen.generate_json_report(
        queries=queries,
        pid=config.pid,
        threshold_ms=config.threshold_ms,
    )

    summary = collector.get_summary()
    logger.info(f"\n{'='*60}")
    logger.info(f"RESULTS SUMMARY")
    logger.info(f"{'='*60}")
    logger.info(f"  MySQL version:    {version.major}")
    logger.info(f"  Total slow queries: {summary['total_queries']}")
    logger.info(f"  Total events:       {summary['total_events']}")
    logger.info(f"  Avg duration:       {summary['avg_duration_ms']:.2f} ms")
    logger.info(f"  Max duration:       {summary['max_duration_ms']:.2f} ms")
    logger.info(f"  P95 duration:       {summary['p95_duration_ms']:.2f} ms")
    logger.info(f"  P99 duration:       {summary['p99_duration_ms']:.2f} ms")
    logger.info(f"  Avg I/O wait:       {summary['avg_io_wait_ms']:.2f} ms")
    logger.info(f"  Avg lock wait:      {summary['avg_lock_wait_ms']:.2f} ms")
    logger.info(f"  Total samples:      {summary['total_samples']}")
    if ai_analyzer and ai_analyzer.enabled:
        logger.info(f"  AI analyzed:        {ai_queries_analyzed} queries")
    logger.info(f"{'='*60}")
    logger.info(f"  HTML report:  {os.path.abspath(html_path)}")
    logger.info(f"  JSON data:    {os.path.abspath(json_path)}")
    logger.info(f"{'='*60}")


def main():
    parser = argparse.ArgumentParser(
        prog="mysql-slow-tracer",
        description="MySQL Slow Query Root Cause Tracer (eBPF-based) - MySQL 5.7/8.0 compatible",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic usage
  sudo python -m mysql_slow_tracer.cli --pid 1234

  # Enable AI analysis (requires local Ollama with qwen2.5-coder:7b)
  sudo python -m mysql_slow_tracer.cli --pid 1234 --ai

  # Use a different Ollama model
  sudo python -m mysql_slow_tracer.cli --pid 1234 --ai --ai-model qwen2.5-coder:14b

  # Detect symbols and probe addresses
  sudo python -m mysql_slow_tracer.cli --pid 1234 --detect

  # Specify MySQL version manually
  sudo python -m mysql_slow_tracer.cli --pid 1234 --mysql-version 5.7

  # Manual symbol alias (use when auto-detection fails)
  sudo python -m mysql_slow_tracer.cli --pid 1234 \\
    --func-alias dispatch_command=_Z16dispatch_commandP3THDPK8COM_DATA19enum_server_command

  # Manual offset (for stripped binaries without debug symbols)
  sudo python -m mysql_slow_tracer.cli --pid 1234 \\
    --func-offset dispatch_command=0x123456 \\
    --func-offset mysql_execute_command=0x789abc

  # Use config file
  sudo python -m mysql_slow_tracer.cli --pid 1234 --config ./config.json
        """,
    )

    parser.add_argument(
        "--pid", type=int, required=True, help="MySQL process ID to trace"
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=100,
        help="Query execution threshold in ms (default: 100)",
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=60,
        help="Tracing duration in seconds (default: 60)",
    )
    parser.add_argument(
        "--mysql-binary",
        type=str,
        default=None,
        help="Path to MySQL binary (default: auto-detect from PID)",
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        default="./reports",
        help="Output directory for reports (default: ./reports)",
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=49,
        help="Sampling rate in Hz (default: 49)",
    )
    parser.add_argument(
        "--max-stack-depth",
        type=int,
        default=64,
        help="Maximum stack trace depth (default: 64)",
    )
    parser.add_argument(
        "--no-io",
        action="store_true",
        help="Disable I/O wait tracking",
    )
    parser.add_argument(
        "--no-locks",
        action="store_true",
        help="Disable lock wait tracking",
    )
    parser.add_argument(
        "--max-events",
        type=int,
        default=10000,
        help="Maximum number of events to buffer (default: 10000)",
    )

    parser.add_argument(
        "--mysql-version",
        type=str,
        default=None,
        choices=["5.5", "5.6", "5.7", "8.0"],
        help="Force MySQL version for symbol mapping (default: auto-detect)",
    )

    parser.add_argument(
        "--detect",
        action="store_true",
        help="Detect and display MySQL symbols, offsets, and version. Exit after detection.",
    )

    parser.add_argument(
        "--list-symbols",
        action="store_true",
        help="List available MySQL symbols from the binary and exit.",
    )

    parser.add_argument(
        "--func-alias",
        type=str,
        action="append",
        default=None,
        metavar="LOGICAL=SYMBOL",
        help="Map a logical function name to an actual symbol name. "
             "Can be used multiple times.",
    )

    parser.add_argument(
        "--func-offset",
        type=str,
        action="append",
        default=None,
        metavar="LOGICAL=0xHEX",
        help="Map a logical function name to a hex offset in the binary. "
             "Can be used multiple times.",
    )

    parser.add_argument(
        "--config",
        type=str,
        default=None,
        help="Path to JSON configuration file",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="Enable verbose logging"
    )

    ai_group = parser.add_argument_group("AI Analysis (Ollama)")
    ai_group.add_argument(
        "--ai",
        action="store_true",
        default=False,
        help="Enable AI-based slow query optimization analysis via local Ollama "
             "(default: off, consumes significant CPU)",
    )
    ai_group.add_argument(
        "--ai-model",
        type=str,
        default="qwen2.5-coder:7b",
        help="Ollama model name for AI analysis (default: qwen2.5-coder:7b)",
    )
    ai_group.add_argument(
        "--ai-url",
        type=str,
        default="http://localhost:11434",
        help="Ollama API base URL (default: http://localhost:11434)",
    )
    ai_group.add_argument(
        "--ai-timeout",
        type=int,
        default=120,
        help="Timeout in seconds for each AI analysis request (default: 120)",
    )
    ai_group.add_argument(
        "--ai-max-concurrent",
        type=int,
        default=2,
        help="Maximum concurrent AI analysis requests (default: 2)",
    )

    args = parser.parse_args()

    setup_logging(args.verbose)

    if not BCC_AVAILABLE:
        logger.error(
            "BCC is not installed. Please install BCC tools:\n"
            "  Ubuntu/Debian: sudo apt install bpfcc-tools python3-bpfcc\n"
            "  RHEL/CentOS:   sudo yum install bcc-tools python3-bcc\n"
            "  See: https://github.com/iovisor/bcc/blob/master/INSTALL.md"
        )
        sys.exit(1)

    if os.geteuid() != 0:
        logger.error(
            "This tool requires root privileges for eBPF operations. "
            "Please run with sudo."
        )
        sys.exit(1)

    if args.config and os.path.exists(args.config):
        config = Config.from_json_file(args.config)
    else:
        config = Config()

    config.pid = args.pid
    config.threshold_ms = args.threshold
    config.duration = args.duration
    config.sample_rate = args.sample_rate
    config.max_stack_depth = args.max_stack_depth
    config.output_dir = args.output_dir
    config.max_events = args.max_events
    config.capture_io = not args.no_io
    config.capture_locks = not args.no_locks
    config.verbose = args.verbose

    if args.mysql_version:
        config.mysql_version = args.mysql_version

    config.func_aliases = parse_func_alias(args.func_alias)
    config.func_offsets = parse_func_offset(args.func_offset)

    if args.mysql_binary:
        config.mysql_binary = args.mysql_binary
    else:
        detected = detect_mysql_binary(config.pid)
        if detected:
            config.mysql_binary = detected
            logger.info(f"Auto-detected MySQL binary: {detected}")
        else:
            logger.warning(
                f"Cannot auto-detect MySQL binary for PID {config.pid}. "
                f"Using default: {config.mysql_binary}"
            )

    errors = config.validate()
    if errors:
        for err in errors:
            logger.error(f"Configuration error: {err}")
        sys.exit(1)

    if not validate_pid(config.pid):
        logger.error(f"PID {config.pid} does not exist or is not accessible")
        sys.exit(1)

    os.makedirs(config.output_dir, exist_ok=True)

    ai_analyzer = None
    if args.ai:
        ollama = OllamaClient(
            base_url=args.ai_url,
            model=args.ai_model,
            timeout=args.ai_timeout,
        )
        ai_analyzer = AIAnalyzer(
            ollama=ollama,
            mysql_version=config.get_effective_version(),
            enabled=True,
            max_concurrent=args.ai_max_concurrent,
        )

        ok, msg = ai_analyzer.check_and_report()
        if ok:
            logger.info(f"AI check passed: {msg}")
        else:
            logger.error(f"AI check failed: {msg}")
            logger.error("Falling back to tracing without AI analysis.")
            ai_analyzer = None

    symbol_resolver = MySQLSymbolResolver(config.mysql_binary)
    logger.info("Loading MySQL symbols...")
    symbol_resolver.load_symbols()

    if args.list_symbols:
        print(symbol_resolver.get_symbol_table_summary())
        return

    if args.detect:
        unresolved = cmd_detect(config, symbol_resolver)
        if unresolved:
            logger.warning(
                f"{len(unresolved)} function(s) could not be resolved. "
                f"Use --func-alias or --func-offset to specify them."
            )
        return

    run_tracer(config, ai_analyzer=ai_analyzer)


if __name__ == "__main__":
    main()
