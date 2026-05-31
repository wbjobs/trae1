# eBPF Syscall Monitor

A Python + FastAPI backend that uses the BCC eBPF framework to monitor the
`openat`, `read`, `write`, and `execve` system calls of a given Linux process
(and all of its descendants) in real time. Captured events are persisted to
PostgreSQL and can be queried through a REST API. A companion CLI tool prints
events in a strace-like style.

When any syscall takes longer than the configured threshold (default **10 ms**),
the backend emits an **alert** — persisted to the `alerts` table, logged, and
pushed over WebSocket to the browser dashboard. The CLI highlights slow calls
in **red**.

## Components

| File | Purpose |
|------|---------|
| `ebpf_monitor/monitor.py` | BCC-based eBPF tracepoint program + perf-buffer polling + process-tree inheritance. |
| `ebpf_monitor/alerts.py`  | Slow-syscall alert broker: persistence + WebSocket broadcast + rolling in-memory buffer. |
| `ebpf_monitor/manager.py` | Background monitor manager writing to PostgreSQL and feeding alerts. |
| `ebpf_monitor/models.py`  | SQLAlchemy `SyscallEvent` and `Alert` models. |
| `ebpf_monitor/queries.py` | Statistics helpers (counts, avg duration, top files). |
| `ebpf_monitor/app.py`     | FastAPI REST + WebSocket + static dashboard. |
| `ebpf_monitor/cli.py`     | CLI (`python -m ebpf_monitor trace/query`). |
| `static/index.html`       | Lightweight dashboard (WebSocket live alerts + stats). |
| `schema.sql`              | PostgreSQL schema. |

## Requirements

- Linux (BCC/eBPF require Linux)
- Python 3.10+
- BCC installed (`python3-bcc` package)
- PostgreSQL 12+

## Install

```bash
pip install -r requirements.txt
```

Initialize the database:

```bash
psql -U postgres -f schema.sql
cp .env.example .env
```

Configuration (`.env`):

| Variable | Default | Description |
|----------|---------|-------------|
| `SLOW_THRESHOLD_MS` | `10` | Syscalls taking longer than this trigger an alert. |

## Run the API

```bash
sudo -E python run_api.py
```

`root` or `CAP_BPF` is required because loading eBPF programs needs elevated
privileges. The server exposes:

- REST API on `http://0.0.0.0:8000/`
- WebSocket at `ws://0.0.0.0:8000/ws/alerts`
- Dashboard at `http://0.0.0.0:8000/dashboard`

### API endpoints

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/monitors` | Start monitoring a PID (`{"pid": 1234}`). |
| `DELETE` | `/monitors/{pid}` | Stop monitoring. |
| `GET` | `/monitors` | List active monitors. |
| `GET` | `/processes/{pid}/syscalls/stats` | Aggregate stats (counts, avg duration, top 5 files). |
| `GET` | `/processes/{pid}/syscalls` | Paginated raw event list. |
| `GET` | `/alerts` | Historical alerts (filter by `pid`, `start`, `end`, `offset`, `limit`). |
| `GET` | `/alerts/recent` | Rolling in-memory alert buffer (does not hit the DB). |
| `GET` | `/dashboard` | Serve the Web dashboard. |
| `WS`  | `/ws/alerts` | Live WebSocket stream of slow-syscall alerts. |

Query parameters: `start`, `end` (ISO 8601 UTC, default last 1h),
`syscall_name`, `offset`, `limit`.

## CLI - real-time trace (strace-lite)

```bash
sudo -E python -m ebpf_monitor trace --pid 1234
sudo -E python -m ebpf_monitor trace --pid 1234 --json
sudo -E python -m ebpf_monitor trace --pid 1234 --duration 30
sudo -E python -m ebpf_monitor trace --pid 1234 --db
```

Lines whose syscall exceeds the slow threshold (10 ms) are printed in **red**
(in terminal mode) and marked `"slow": true` in JSON mode.

## CLI - query statistics from the database

```bash
python -m ebpf_monitor query --pid 1234 --since 1h
python -m ebpf_monitor query --pid 1234 --since 30m --json
```

## Web dashboard

Open `http://0.0.0.0:8000/dashboard` in your browser. The page:

- Connects to `/ws/alerts` and auto-reconnects on disconnect.
- Shows rolling alert cards (flash-highlighted on arrival).
- Maintains a per-syscall breakdown of slow calls.
- Displays a live log of the last 100 alert messages.
- Seeds itself on load from `GET /alerts/recent` so the view is not empty.

## How the eBPF side works

The BPF program attaches to four portable Linux tracepoints:

- `raw_syscalls:sys_enter` — captures the syscall number (only
  `openat`=257, `read`=0, `write`=1, `execve`=59 are forwarded), the
  filename pointer where applicable, and stores a `data_t` record keyed
  by `pid_tgid` in a BPF hash map.
- `raw_syscalls:sys_exit` — looks up the entry, computes the duration
  in nanoseconds, reads the return value, and submits the completed
  event through a perf ring buffer.
- `sched:sched_process_fork` — when a monitored process forks a child,
  automatically inserts the child's TGID into the `monitored` BPF hash
  set so that the whole process tree is captured.
- `sched:sched_process_exit` — removes the TGID from the `monitored`
  set on exit to keep the map bounded.

The filter in `sys_enter` checks `monitored.lookup(&tgid)` instead of
a hard-coded PID. On startup, userspace seeds the `monitored` map with
the target PID and any already-existing descendants found via
`/proc/<pid>/task/*/children` (or by scanning `/proc/*/status` on older
kernels).

## Slow-syscall alert flow

1. BPF emits a syscall event with `duration_ns`.
2. `MonitorManager._persist` calls `AlertManager.handle_event`.
3. `AlertManager` checks `duration_ns >= SLOW_THRESHOLD_MS * 1_000_000`.
4. On a match it:
   - appends to a rolling in-memory buffer,
   - spawns a background thread that (a) persists the alert to the
     `alerts` table, (b) logs via Python `logging`, and (c) broadcasts
     the JSON payload on the server's asyncio event loop to every
     connected WebSocket.
5. The dashboard's `<script>` renders the alert and highlights it.

The persistence step and the WebSocket broadcast are each wrapped in
their own `try/except` — a slow DB or a dead WebSocket client cannot
crash the monitor loop.

## Example

Start the API, launch a long-running process (e.g. `sleep 60`), then:

```bash
curl -X POST http://localhost:8000/monitors \
     -H 'Content-Type: application/json' -d '{"pid": 1234}'

# exercise the process to generate syscalls ...

curl 'http://localhost:8000/processes/1234/syscalls/stats?start=2025-01-01T00:00:00Z&end=2025-01-02T00:00:00Z'

curl 'http://localhost:8000/alerts/recent'

# Open the dashboard in a browser:
#   http://localhost:8000/dashboard
```
