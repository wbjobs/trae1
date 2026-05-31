"""
eBPF syscall monitor using BCC.

Attaches to the portable ``raw_syscalls:sys_enter`` and
``raw_syscalls:sys_exit`` tracepoints to monitor four syscalls —
``openat``, ``read``, ``write``, ``execve`` — for a specific PID and
all of its descendant processes (children, grandchildren, ...).

The monitored process set is kept in a BPF hash map called ``monitored``.
A ``sched:sched_process_fork`` tracepoint automatically adds newly created
children of any monitored process to the set. A ``sched:sched_process_exit``
tracepoint removes entries when processes terminate.

Events are published through a perf ring buffer to userspace.
"""
from __future__ import annotations

import ctypes as ct
import os
import threading
import time
from dataclasses import dataclass
from glob import glob
from typing import Callable, Dict, List, Optional, Set

try:
    from bcc import BPF  # type: ignore
    _HAS_BCC = True
except Exception:  # pragma: no cover - import guard
    BPF = None  # type: ignore
    _HAS_BCC = False


BPF_SOURCE = r"""
#include <uapi/linux/ptrace.h>
#include <linux/sched.h>

#define SYSCALL_OPENAT  1
#define SYSCALL_READ    2
#define SYSCALL_WRITE   3
#define SYSCALL_EXECVE  4

#define FNAME_LEN 256

struct data_t {
    u32 pid;
    u32 tid;
    char comm[TASK_COMM_LEN];
    u8  syscall;
    u64 timestamp_ns;
    u64 duration_ns;
    s64 ret;
    char filename[FNAME_LEN];
    u64 arg1;
    u64 arg2;
    u64 arg3;
};

BPF_HASH(start, u64, struct data_t);
BPF_PERF_OUTPUT(events);

/* Map of TGIDs that belong to the monitored process tree. */
BPF_HASH(monitored, u32, u8);

static __always_inline void init_data(struct data_t *d,
                                      u32 pid, u32 tid,
                                      const char *comm, u8 syscall) {
    d->pid = pid;
    d->tid = tid;
    __builtin_memcpy(d->comm, comm, TASK_COMM_LEN);
    d->syscall = syscall;
    d->timestamp_ns = bpf_ktime_get_ns();
    d->duration_ns = 0;
    d->ret = 0;
    d->filename[0] = 0;
    d->arg1 = 0;
    d->arg2 = 0;
    d->arg3 = 0;
}

TRACEPOINT_PROBE(raw_syscalls, sys_enter) {
    u64 id = bpf_get_current_pid_tgid();
    u32 pid = id >> 32;
    u32 tid = (u32)id;

    /* Only forward events from processes in the monitored set. */
    if (monitored.lookup(&pid) == NULL) {
        return 0;
    }

    long sys_num = args->id;

    char comm[TASK_COMM_LEN];
    bpf_get_current_comm(&comm, sizeof(comm));

    struct data_t data = {};
    const char __user *filename = NULL;

    switch (sys_num) {
        case 257: /* openat */
            init_data(&data, pid, tid, comm, SYSCALL_OPENAT);
            filename = (const char __user *)args->args[1];
            data.arg1 = (u64)args->args[0];
            data.arg2 = (u64)args->args[2];
            data.arg3 = (u64)args->args[3];
            break;
        case 0: /* read */
            init_data(&data, pid, tid, comm, SYSCALL_READ);
            data.arg1 = (u64)args->args[0];
            data.arg2 = (u64)args->args[1];
            data.arg3 = (u64)args->args[2];
            break;
        case 1: /* write */
            init_data(&data, pid, tid, comm, SYSCALL_WRITE);
            data.arg1 = (u64)args->args[0];
            data.arg2 = (u64)args->args[1];
            data.arg3 = (u64)args->args[2];
            break;
        case 59: /* execve */
            init_data(&data, pid, tid, comm, SYSCALL_EXECVE);
            filename = (const char __user *)args->args[0];
            data.arg1 = (u64)args->args[0];
            data.arg2 = (u64)args->args[1];
            data.arg3 = (u64)args->args[2];
            break;
        default:
            return 0;
    }

    if (filename != NULL) {
        bpf_probe_read_user_str(&data.filename, sizeof(data.filename), filename);
    }

    start.update(&id, &data);
    return 0;
}

TRACEPOINT_PROBE(raw_syscalls, sys_exit) {
    u64 id = bpf_get_current_pid_tgid();
    struct data_t *dp = start.lookup(&id);
    if (dp == NULL)
        return 0;

    u64 end_ts = bpf_ktime_get_ns();
    dp->duration_ns = end_ts - dp->timestamp_ns;
    dp->ret = args->ret;

    events.perf_submit(args, dp, sizeof(*dp));
    start.delete(&id);
    return 0;
}

/*
 * When a monitored process forks/clones a new child, auto-add the child
 * TGID to the monitored set so that its syscalls are also captured.
 * On process exit, remove the entry to keep the map bounded.
 */
TRACEPOINT_PROBE(sched, sched_process_fork) {
    u32 parent_pid = args->parent_pid;
    u32 child_pid  = args->child_pid;

    if (monitored.lookup(&parent_pid) == NULL)
        return 0;

    u8 one = 1;
    monitored.update(&child_pid, &one);
    return 0;
}

TRACEPOINT_PROBE(sched, sched_process_exit) {
    u32 pid = args->pid;
    monitored.delete(&pid);
    return 0;
}
"""


class DataEvent(ct.Structure):
    _fields_ = [
        ("pid", ct.c_uint),
        ("tid", ct.c_uint),
        ("comm", ct.c_char * 16),
        ("syscall", ct.c_ubyte),
        ("timestamp_ns", ct.c_ulonglong),
        ("duration_ns", ct.c_ulonglong),
        ("ret", ct.c_longlong),
        ("filename", ct.c_char * 256),
        ("arg1", ct.c_ulonglong),
        ("arg2", ct.c_ulonglong),
        ("arg3", ct.c_ulonglong),
    ]


SYSCALL_MAP = {
    1: "openat",
    2: "read",
    3: "write",
    4: "execve",
}


@dataclass
class SyscallEvent:
    pid: int
    tid: int
    comm: str
    syscall_name: str
    timestamp_ns: int
    duration_ns: int
    return_value: int
    file_path: Optional[str]
    arg1: int
    arg2: int
    arg3: int


EventHandler = Callable[[SyscallEvent], None]


def _read_children_via_proc(pid: int) -> Set[int]:
    """Best-effort walk of the process tree under *pid*.

    Reads ``/proc/<pid>/task/*/children`` if available (kernel 4.14+),
    otherwise falls back to scanning ``/proc/*/status`` for ``PPid``.
    Returns a set of descendant TGIDs (excluding the root *pid* itself).
    """
    found: Set[int] = set()
    # 1) try the /proc children file
    for task_children in glob(f"/proc/{pid}/task/*/children"):
        try:
            with open(task_children, "r") as fh:
                for token in fh.read().split():
                    try:
                        found.add(int(token))
                    except ValueError:
                        pass
        except OSError:
            continue
    # 2) fallback: scan /proc/*/status for PPid
    if not found:
        try:
            pids_in_proc = {
                int(d) for d in os.listdir("/proc") if d.isdigit()
            }
        except OSError:
            return found
        parent_of: Dict[int, int] = {}
        for p in pids_in_proc:
            try:
                with open(f"/proc/{p}/status", "r") as fh:
                    for line in fh:
                        if line.startswith("PPid:"):
                            try:
                                parent_of[p] = int(line.split()[1])
                            except (IndexError, ValueError):
                                pass
                            break
            except OSError:
                continue
        # BFS from target pid
        stack = [pid]
        while stack:
            cur = stack.pop()
            for child_ppid, parent_ppid in parent_of.items():
                if parent_ppid == cur and child_ppid not in found:
                    found.add(child_ppid)
                    stack.append(child_ppid)
    return found


class SyscallMonitor:
    """BCC-backed monitor for openat/read/write/execve of a PID and all
    of its descendants (children, grandchildren, ...)."""

    def __init__(self, pid: int, handler: EventHandler, poll_interval: float = 0.1):
        if not _HAS_BCC:
            raise RuntimeError(
                "BCC is not installed. Please install bcc and run as root."
            )
        self.pid = int(pid)
        self.handler = handler
        self.poll_interval = poll_interval
        self._bpf: Optional["BPF"] = None
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._initially_seeded: Set[int] = set()

    # ------------------------------------------------------------------
    # BPF source
    # ------------------------------------------------------------------
    def _build_source(self) -> str:
        return BPF_SOURCE

    # ------------------------------------------------------------------
    # Perf buffer
    # ------------------------------------------------------------------
    def _handle_event(self, cpu, data, size):  # noqa: ARG002
        event = ct.cast(data, ct.POINTER(DataEvent)).contents
        sys_name = SYSCALL_MAP.get(event.syscall, f"unknown({event.syscall})")
        try:
            filename = event.filename.decode("utf-8", errors="replace")
        except Exception:
            filename = ""
        if not filename:
            filename = None
        evt = SyscallEvent(
            pid=event.pid,
            tid=event.tid,
            comm=event.comm.decode("utf-8", errors="replace") or "",
            syscall_name=sys_name,
            timestamp_ns=int(event.timestamp_ns),
            duration_ns=int(event.duration_ns),
            return_value=int(event.ret),
            file_path=filename,
            arg1=int(event.arg1),
            arg2=int(event.arg2),
            arg3=int(event.arg3),
        )
        try:
            self.handler(evt)
        except Exception:
            pass

    def _handle_lost(self, cpu, lost):  # noqa: ARG002
        pass

    # ------------------------------------------------------------------
    # Seeding the monitored set
    # ------------------------------------------------------------------
    def _seed_monitored_map(self) -> None:
        """Populate the ``monitored`` BPF map with the target PID and any
        already-existing descendants so they are captured from the start."""
        assert self._bpf is not None
        monitored = self._bpf["monitored"]
        one = ct.c_ubyte(1)

        target_key = ct.c_uint(self.pid)
        monitored[target_key] = one
        self._initially_seeded.add(self.pid)

        try:
            descendants = _read_children_via_proc(self.pid)
        except Exception:
            descendants = set()

        for child_pid in descendants:
            try:
                monitored[ct.c_uint(child_pid)] = one
                self._initially_seeded.add(child_pid)
            except Exception:
                pass

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------
    def start(self):
        if self._bpf is not None:
            raise RuntimeError("Monitor already started")
        src = self._build_source()
        bpf = BPF(text=src)

        bpf["events"].open_perf_buffer(
            self._handle_event, lost_cb=self._handle_lost, page_cnt=64
        )

        self._bpf = bpf
        self._seed_monitored_map()
        self._stop.clear()

    def poll_once(self):
        if self._bpf is None:
            raise RuntimeError("Monitor not started")
        self._bpf.perf_buffer_poll(timeout=0)

    def run_forever(self):
        if self._bpf is None:
            self.start()
        while not self._stop.is_set():
            if self._bpf is None:
                break
            try:
                self._bpf.perf_buffer_poll(timeout=int(self.poll_interval * 1000))
            except KeyboardInterrupt:
                break

    def run_forever_async(self) -> threading.Thread:
        if self._thread is not None and self._thread.is_alive():
            return self._thread
        if self._bpf is None:
            self.start()
        self._thread = threading.Thread(target=self.run_forever, daemon=True)
        self._thread.start()
        return self._thread

    def stop(self):
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2)
        if self._bpf is not None:
            try:
                self._bpf.cleanup()
            except Exception:
                pass
        self._bpf = None

    def is_running(self) -> bool:
        return self._bpf is not None and not self._stop.is_set()

    def currently_monitored(self) -> Set[int]:
        """Return the current set of TGIDs present in the BPF map.

        Only works while the monitor is running.
        """
        if self._bpf is None:
            return set()
        result: Set[int] = set()
        try:
            monitored = self._bpf["monitored"]
            for k in monitored.keys():
                result.add(int(k.value))
        except Exception:
            pass
        return result


def describe_event(event: SyscallEvent) -> str:
    parts: List[str] = []
    if event.file_path:
        parts.append(event.file_path)
    if event.syscall_name in ("read", "write"):
        parts.append(
            f"fd={event.arg1} buf=0x{event.arg2:x} count={event.arg3}"
        )
    elif event.syscall_name == "openat":
        parts.append(f"flags={event.arg2} mode={event.arg3}")
    elif event.syscall_name == "execve":
        parts.append(f"argv=0x{event.arg2:x} envp=0x{event.arg3:x}")
    args = ", ".join(parts)
    dur = event.duration_ns / 1000.0
    return (
        f"[{time.strftime('%H:%M:%S')}] pid={event.pid} comm={event.comm} "
        f"{event.syscall_name}({args}) = {event.return_value} "
        f"<{dur:.2f} us>"
    )
