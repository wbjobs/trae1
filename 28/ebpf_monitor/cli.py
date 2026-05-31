"""
Command line tool similar to strace that prints openat/read/write/execve
events for a target PID in real time using eBPF via BCC.

Must be run as root (requires CAP_BPF / CAP_SYS_ADMIN).

Examples:
    sudo python -m ebpf_monitor.cli --pid 1234
    sudo python -m ebpf_monitor.cli --pid 1234 --json
    sudo python -m ebpf_monitor.cli --pid 1234 --db
"""
from __future__ import annotations

import json
import sys
import time
from datetime import datetime, timedelta, timezone

import click

from .monitor import SyscallEvent, SyscallMonitor, describe_event

SLOW_THRESHOLD_MS = 10


def _is_slow(event: SyscallEvent) -> bool:
    return event.duration_ns >= SLOW_THRESHOLD_MS * 1_000_000


def _print_json(event: SyscallEvent) -> None:
    payload = {
        "timestamp": datetime.fromtimestamp(
            event.timestamp_ns / 1e9, tz=timezone.utc
        ).isoformat(),
        "pid": event.pid,
        "tid": event.tid,
        "comm": event.comm,
        "syscall": event.syscall_name,
        "return_value": event.return_value,
        "duration_ns": event.duration_ns,
        "duration_ms": event.duration_ns / 1_000_000,
        "slow": _is_slow(event),
        "threshold_ms": SLOW_THRESHOLD_MS,
        "file_path": event.file_path,
        "arg1": event.arg1,
        "arg2": event.arg2,
        "arg3": event.arg3,
    }
    click.echo(json.dumps(payload, default=str))


@click.group()
def cli():
    """eBPF syscall monitoring tools."""


@cli.command("trace")
@click.option("--pid", required=True, type=int, help="Target PID to monitor.")
@click.option(
    "--json", "as_json", is_flag=True, help="Emit events as JSON lines."
)
@click.option(
    "--db",
    "use_db",
    is_flag=True,
    help="Also persist captured events into PostgreSQL.",
)
@click.option(
    "--duration",
    type=float,
    default=0,
    help="Stop after N seconds (0 = until Ctrl+C).",
)
def trace_cmd(pid: int, as_json: bool, use_db: bool, duration: float):
    """Print real-time syscall events for a process (strace-lite)."""
    if use_db:
        try:
            from .manager import get_monitor_manager

            mgr = get_monitor_manager()
            if not mgr.is_running(pid):
                mgr.start(pid)
            click.echo(
                f"[db] monitor started for pid={pid}; persisting to PostgreSQL",
                err=True,
            )
        except Exception as exc:  # pragma: no cover
            click.echo(f"[db] failed: {exc}", err=True)

    def handler(event: SyscallEvent) -> None:
        if as_json:
            _print_json(event)
            return
        line = describe_event(event)
        if _is_slow(event):
            click.echo(click.style(line, fg="red", bold=True))
        else:
            click.echo(line)

    mon = SyscallMonitor(pid=pid, handler=handler)
    click.echo(
        f"[ebpf] monitoring pid={pid} for openat/read/write/execve. Ctrl+C to stop.",
        err=True,
    )
    try:
        if duration and duration > 0:
            mon.start()
            end = time.time() + duration
            while time.time() < end:
                mon.poll_once()
                time.sleep(0.05)
        else:
            mon.run_forever()
    except KeyboardInterrupt:
        click.echo("\n[ebpf] stopping...", err=True)
    finally:
        mon.stop()


@cli.command("query")
@click.option("--pid", required=True, type=int, help="Target PID to query.")
@click.option(
    "--since",
    type=str,
    default="1h",
    help="Look-back window: e.g. 1h, 30m, 2h (default 1h).",
)
@click.option(
    "--limit", type=int, default=20, help="Number of events to list (default 20)."
)
@click.option("--json", "as_json", is_flag=True, help="Emit output as JSON.")
def query_cmd(pid: int, since: str, limit: int, as_json: bool):
    """Query syscall statistics from the PostgreSQL database."""
    try:
        from .database import SessionLocal
        from .queries import count_total_calls, list_events, stats_per_syscall, top_files
    except Exception as exc:  # pragma: no cover
        click.echo(f"Failed to import database modules: {exc}", err=True)
        sys.exit(1)

    unit = since[-1]
    try:
        amount = int(since[:-1])
    except ValueError:
        raise click.BadParameter("--since must end with m/h, e.g. 30m")
    now = datetime.now(tz=timezone.utc)
    if unit == "m":
        start = now - timedelta(minutes=amount)
    elif unit == "h":
        start = now - timedelta(hours=amount)
    elif unit == "d":
        start = now - timedelta(days=amount)
    else:
        raise click.BadParameter("--since must end with m/h/d")

    session = SessionLocal()
    try:
        total = count_total_calls(session, pid, start, now)
        per = stats_per_syscall(session, pid, start, now)
        tops = top_files(session, pid, start, now, limit=5)
        events, _ = list_events(session, pid, start, now, offset=0, limit=limit)
    finally:
        session.close()

    payload = {
        "pid": pid,
        "start": start.isoformat(),
        "end": now.isoformat(),
        "total_calls": total,
        "per_syscall": [p.dict() for p in per],
        "top_files": [t.dict() for t in tops],
        "events": [
            {
                "id": e.id,
                "timestamp": e.timestamp.isoformat(),
                "syscall": e.syscall_name,
                "ret": e.return_value,
                "duration_ns": e.duration_ns,
                "file_path": e.file_path,
            }
            for e in events
        ],
    }
    if as_json:
        click.echo(json.dumps(payload, indent=2, default=str))
        return

    click.echo(f"PID {pid}  [{start.isoformat()} -> {now.isoformat()}]")
    click.echo(f"Total calls: {total}")
    click.echo("Per-syscall:")
    for p in per:
        avg_us = (p.avg_duration_ns or 0) / 1000.0
        click.echo(f"  {p.syscall_name:<8s}  count={p.count:<6d}  avg={avg_us:8.2f} us")
    click.echo("Top 5 files:")
    for t in tops:
        click.echo(f"  {t.count:<6d}  {t.file_path}")
    click.echo(f"\nLatest {len(events)} events:")
    for e in events:
        click.echo(
            f"  {e.timestamp.isoformat()}  {e.syscall_name:<8s}  "
            f"ret={e.return_value}  dur={((e.duration_ns or 0) / 1000):.2f} us  "
            f"{e.file_path or '-'}"
        )


def main():  # pragma: no cover
    cli()


if __name__ == "__main__":  # pragma: no cover
    main()
