"""BCC/eBPF core tracer for MySQL slow query analysis.

Attaches uprobes to MySQL functions using version-aware symbol resolution.
Supports both symbol names and manual offsets for stripped binaries.
"""

import ctypes
import logging
import os
import time
from typing import Any, Callable, Dict, List, Optional, Tuple

from .config import Config
from .symbols import MySQLSymbolResolver
from .version import FunctionMapper, MySQLVersionDetector, FunctionMapping

logger = logging.getLogger(__name__)

try:
    from bcc import BPF, USDT
    BCC_AVAILABLE = True
except ImportError:
    BCC_AVAILABLE = False
    logger.warning("bcc package not installed. Install with: pip install bcc")


BPF_PROGRAM = r"""
#include <uapi/linux/ptrace.h>
#include <linux/sched.h>

#define MAX_QUERY_LEN      512
#define MAX_FUNC_NAME_LEN  128
#define MAX_STACK_DEPTH    64
#define MAX_QUERIES        8192

#define EVENT_QUERY_START   1
#define EVENT_QUERY_END     2
#define EVENT_STACK_SAMPLE  3
#define EVENT_IO_WAIT       4
#define EVENT_LOCK_WAIT     5

struct query_info_t {
    u64 start_ts;
    u64 end_ts;
    u64 io_wait_ns;
    u64 lock_wait_ns;
    u64 io_start_ts;
    u64 lock_start_ts;
    u32 pid;
    u32 tid;
    int   in_io;
    int   in_lock;
    char  query[MAX_QUERY_LEN];
};

struct event_t {
    u32 type;
    u32 pid;
    u32 tid;
    u64 timestamp;
    u64 duration_ns;
    u64 io_wait_ns;
    u64 lock_wait_ns;
    char  func_name[MAX_FUNC_NAME_LEN];
    char  query[MAX_QUERY_LEN];
    int   stack_id;
};

struct func_entry_t {
    u64 start_ts;
    char func_name[MAX_FUNC_NAME_LEN];
};

BPF_HASH(active_queries, u32, struct query_info_t, MAX_QUERIES);
BPF_HASH(func_entries, u32, struct func_entry_t, MAX_QUERIES * 4);
BPF_STACK_TRACE(stack_traces, __STACK_MAP_SIZE__);
BPF_PERF_OUTPUT(events);
BPF_ARRAY(config_threshold, u64, 1);
BPF_ARRAY(event_count, u64, 1);

static __always_inline u64 get_timestamp_ns(void) {
    return bpf_ktime_get_ns();
}

static __always_inline int should_sample(struct query_info_t *qi) {
    if (!qi) return 0;
    u64 elapsed = get_timestamp_ns() - qi->start_ts;
    u64 *thresh = config_threshold.lookup(&(u32){0});
    if (thresh && elapsed > *thresh) {
        return 1;
    }
    return 0;
}

int trace_dispatch_entry(struct pt_regs *ctx) {
    u32 tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    u64 ts = get_timestamp_ns();

    struct query_info_t qi = {};
    qi.start_ts = ts;
    qi.end_ts = 0;
    qi.io_wait_ns = 0;
    qi.lock_wait_ns = 0;
    qi.io_start_ts = 0;
    qi.lock_start_ts = 0;
    qi.pid = (__TARGET_PID__);
    qi.tid = tid;
    qi.in_io = 0;
    qi.in_lock = 0;

    void *sp = (void *)PT_REGS_SP(ctx);
    void *user_data = NULL;

    if (bpf_probe_read_user(&user_data, sizeof(user_data),
                            sp + 8) < 0) {
        user_data = NULL;
    }
    if (user_data) {
        bpf_probe_read_user_str(&qi.query, sizeof(qi.query), user_data);
    }

    active_queries.update(&tid, &qi);
    return 0;
}

int trace_dispatch_exit(struct pt_regs *ctx) {
    u32 tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    u64 ts = get_timestamp_ns();

    struct query_info_t *qip = active_queries.lookup(&tid);
    if (!qip) return 0;

    u64 duration = ts - qip->start_ts;
    u64 *thresh = config_threshold.lookup(&(u32){0});

    if (thresh && duration > *thresh) {
        struct event_t event = {};
        event.type = EVENT_QUERY_END;
        event.pid = qip->pid;
        event.tid = tid;
        event.timestamp = qip->start_ts;
        event.duration_ns = duration;
        event.io_wait_ns = qip->io_wait_ns;
        event.lock_wait_ns = qip->lock_wait_ns;
        event.stack_id = -1;
        __builtin_memcpy(event.query, qip->query, MAX_QUERY_LEN);
        events.perf_submit(ctx, &event, sizeof(event));
    }

    active_queries.delete(&tid);
    return 0;
}

int trace_func_entry(struct pt_regs *ctx) {
    u32 tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    u64 ts = get_timestamp_ns();

    struct query_info_t *qip = active_queries.lookup(&tid);
    if (!qip) return 0;

    struct func_entry_t fe = {};
    fe.start_ts = ts;
    __builtin_memcpy(fe.func_name, "__FUNC_NAME__", MAX_FUNC_NAME_LEN);

    u32 key = tid * 1000 + __FUNC_ID__;
    func_entries.update(&key, &fe);

    if (should_sample(qip)) {
        struct event_t event = {};
        event.type = EVENT_STACK_SAMPLE;
        event.pid = qip->pid;
        event.tid = tid;
        event.timestamp = ts;
        event.duration_ns = 0;
        event.io_wait_ns = 0;
        event.lock_wait_ns = 0;
        event.stack_id = stack_traces.get_stackid(ctx, BPF_F_USER_STACK);
        __builtin_memcpy(event.func_name, "__FUNC_NAME__", MAX_FUNC_NAME_LEN);
        __builtin_memcpy(event.query, qip->query, MAX_QUERY_LEN);
        events.perf_submit(ctx, &event, sizeof(event));
    }

    return 0;
}

int trace_func_exit(struct pt_regs *ctx) {
    u32 tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;

    struct query_info_t *qip = active_queries.lookup(&tid);
    if (!qip) return 0;

    u32 key = tid * 1000 + __FUNC_ID__;
    struct func_entry_t *fep = func_entries.lookup(&key);
    if (!fep) return 0;

    u64 dur = get_timestamp_ns() - fep->start_ts;

    if (strncmp("__FUNC_NAME__", "fil_io", 6) == 0 ||
        strncmp("__FUNC_NAME__", "os_file_read", 13) == 0 ||
        strncmp("__FUNC_NAME__", "os_file_write", 14) == 0) {
        qip->io_wait_ns += dur;
    }

    if (strncmp("__FUNC_NAME__", "lock_sec_rec", 11) == 0 ||
        strncmp("__FUNC_NAME__", "lock_table", 10) == 0) {
        qip->lock_wait_ns += dur;
    }

    func_entries.delete(&key);
    return 0;
}

int trace_io_entry(struct pt_regs *ctx) {
    u32 tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    struct query_info_t *qip = active_queries.lookup(&tid);
    if (!qip) return 0;
    qip->io_start_ts = get_timestamp_ns();
    qip->in_io = 1;
    return 0;
}

int trace_io_exit(struct pt_regs *ctx) {
    u32 tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    struct query_info_t *qip = active_queries.lookup(&tid);
    if (!qip) return 0;
    if (qip->in_io) {
        u64 dur = get_timestamp_ns() - qip->io_start_ts;
        qip->io_wait_ns += dur;
        qip->in_io = 0;
    }
    return 0;
}

int trace_lock_entry(struct pt_regs *ctx) {
    u32 tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    struct query_info_t *qip = active_queries.lookup(&tid);
    if (!qip) return 0;
    qip->lock_start_ts = get_timestamp_ns();
    qip->in_lock = 1;
    return 0;
}

int trace_lock_exit(struct pt_regs *ctx) {
    u32 tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF;
    struct query_info_t *qip = active_queries.lookup(&tid);
    if (!qip) return 0;
    if (qip->in_lock) {
        u64 dur = get_timestamp_ns() - qip->lock_start_ts;
        qip->lock_wait_ns += dur;
        qip->in_lock = 0;
    }
    return 0;
}
"""


class TracerEventType:
    """Main eBPF-based MySQL slow query tracer with version-aware probing."""

    EVENT_QUERY_START = 1
    EVENT_QUERY_END = 2
    EVENT_STACK_SAMPLE = 3
    EVENT_IO_WAIT = 4
    EVENT_LOCK_WAIT = 5

    def __init__(
        self,
        config: Config,
        symbol_resolver: MySQLSymbolResolver,
        func_mapper: Optional[FunctionMapper] = None,
        version_detector: Optional[MySQLVersionDetector] = None,
    ):
        if not BCC_AVAILABLE:
            raise RuntimeError(
                "bcc is required. Install BCC tools first: "
                "https://github.com/iovisor/bcc/blob/master/INSTALL.md"
            )
        self.config = config
        self.symbol_resolver = symbol_resolver
        self.bpf: Optional[BPF] = None
        self._event_handlers: List[Dict] = []
        self._dispatch_probe: Optional[FunctionMapping] = None
        self._func_probes: Dict[str, FunctionMapping] = {}
        self._io_probes: Dict[str, FunctionMapping] = {}
        self._lock_probes: Dict[str, FunctionMapping] = {}
        self._dispatch_entry_prog_loaded = False
        self._callbacks: List[Callable] = []

        if version_detector is None:
            version_detector = MySQLVersionDetector(
                config.mysql_binary, config.pid
            )
        self.version_detector = version_detector

        if func_mapper is None:
            func_mapper = FunctionMapper(
                self.version_detector,
                symbol_resolver,
                func_aliases=config.func_aliases,
                func_offsets=config.func_offsets,
            )
        self.func_mapper = func_mapper

    def _build_bpf_program(self) -> str:
        program = BPF_PROGRAM.replace("__TARGET_PID__", str(self.config.pid))
        program = program.replace("__STACK_MAP_SIZE__", str(self.config.stack_map_size))
        return program

    def _resolve_symbols(self):
        version = self.version_detector.detect()
        logger.info(f"Using MySQL version: {version.major} (source: {version.source})")

        self._dispatch_probe = self.func_mapper.resolve("dispatch_command")

        func_names = [f for f in self.config.mysql_funcs if f != "dispatch_command"]
        for name in func_names:
            mapping = self.func_mapper.resolve(name)
            if mapping.has_resolved():
                self._func_probes[name] = mapping

        if self.config.capture_io:
            for name in self.config.io_funcs:
                mapping = self.func_mapper.resolve(name)
                if mapping.has_resolved():
                    self._io_probes[name] = mapping

        if self.config.capture_locks:
            for name in self.config.lock_funcs:
                mapping = self.func_mapper.resolve(name)
                if mapping.has_resolved():
                    self._lock_probes[name] = mapping

        logger.info(f"Resolved dispatch_command: {self._dispatch_probe.resolved_symbol}")
        logger.info(f"Resolved {len(self._func_probes)} MySQL functions")
        logger.info(f"Resolved {len(self._io_probes)} I/O functions")
        logger.info(f"Resolved {len(self._lock_probes)} lock functions")

    def _attach_probe_by_mapping(
        self,
        mapping: FunctionMapping,
        fn_name_entry: str,
        fn_name_exit: Optional[str] = None,
    ) -> bool:
        if not self.bpf:
            return False

        if mapping.offset > 0:
            try:
                self.bpf.attach_uprobe(
                    name=self.config.mysql_binary,
                    addr=mapping.offset,
                    fn_name=fn_name_entry,
                    pid=self.config.pid,
                )
                if fn_name_exit:
                    self.bpf.attach_uretprobe(
                        name=self.config.mysql_binary,
                        addr=mapping.offset,
                        fn_name=fn_name_exit,
                        pid=self.config.pid,
                    )
                logger.info(
                    f"Attached offset probe for {mapping.logical_name} "
                    f"at {hex(mapping.offset)}"
                )
                return True
            except Exception as e:
                logger.warning(
                    f"Failed to attach offset probe for {mapping.logical_name} "
                    f"at {hex(mapping.offset)}: {e}"
                )
                return False

        if mapping.resolved_symbol:
            try:
                self.bpf.attach_uprobe(
                    name=self.config.mysql_binary,
                    sym=mapping.resolved_symbol,
                    fn_name=fn_name_entry,
                    pid=self.config.pid,
                )
                if fn_name_exit:
                    self.bpf.attach_uretprobe(
                        name=self.config.mysql_binary,
                        sym=mapping.resolved_symbol,
                        fn_name=fn_name_exit,
                        pid=self.config.pid,
                    )
                logger.info(
                    f"Attached symbol probe for {mapping.logical_name} "
                    f"-> {mapping.resolved_symbol}"
                )
                return True
            except Exception as e:
                logger.warning(
                    f"Failed to attach probe for {mapping.logical_name} "
                    f"({mapping.resolved_symbol}): {e}"
                )
                return False

        return False

    def _attach_probes(self):
        if not self.bpf:
            return

        if self._dispatch_probe and self._dispatch_probe.has_resolved():
            attached = self._attach_probe_by_mapping(
                self._dispatch_probe,
                "trace_dispatch_entry",
                "trace_dispatch_exit",
            )
            if attached:
                self._dispatch_entry_prog_loaded = True

        func_id = 1
        for logical_name, mapping in self._func_probes.items():
            if func_id >= 999:
                logger.warning("Too many functions, truncating at 999")
                break
            self._attach_probe_by_mapping(
                mapping,
                "trace_func_entry",
                "trace_func_exit",
            )
            func_id += 1

        for logical_name, mapping in self._io_probes.items():
            self._attach_probe_by_mapping(
                mapping,
                "trace_io_entry",
                "trace_io_exit",
            )

        for logical_name, mapping in self._lock_probes.items():
            self._attach_probe_by_mapping(
                mapping,
                "trace_lock_entry",
                "trace_lock_exit",
            )

    def start(self):
        """Initialize and start the tracer."""
        self._resolve_symbols()

        if not self._dispatch_probe or not self._dispatch_probe.has_resolved():
            error_lines = [
                "Cannot find dispatch_command in MySQL binary.",
                "  The binary may be stripped or the symbol name differs.",
                "",
                "Resolution attempts:",
            ]
            if self._dispatch_probe:
                error_lines.append(
                    f"  Logical: {self._dispatch_probe.logical_name}"
                )
                error_lines.append(
                    f"  Resolved: {self._dispatch_probe.resolved_symbol or '(none)'}"
                )
                error_lines.append(
                    f"  Offset: {hex(self._dispatch_probe.offset) if self._dispatch_probe.offset else '(none)'}"
                )
                if self._dispatch_probe.available_symbols:
                    error_lines.append(
                        f"  Candidates: {self._dispatch_probe.available_symbols[:5]}"
                    )
            error_lines.extend([
                "",
                "Suggestions:",
                "  1. Install debug symbols: apt install mysql-server-dbg",
                "  2. Use --detect to auto-detect function addresses",
                "  3. Manually specify offsets in config.json:",
                '     {"func_offsets": {"dispatch_command": "0x123456"}}',
                "  4. Use --func-alias dispatch_command=actual_symbol_name",
            ])
            raise RuntimeError("\n".join(error_lines))

        program = self._build_bpf_program()
        self.bpf = BPF(text=program)

        threshold_ns = self.config.threshold_ms * 1_000_000
        self.bpf["config_threshold"][ctypes.c_int(0)] = ctypes.c_ulonglong(threshold_ns)

        self._attach_probes()

        logger.info("Tracer started successfully")

    def register_callback(self, callback: Callable):
        self._callbacks.append(callback)

    def _perf_buffer_handler(self, cpu, data, size):
        if not self.bpf:
            return
        event = self.bpf["events"].event(data)
        for cb in self._callbacks:
            try:
                cb(event)
            except Exception as e:
                logger.error(f"Callback error: {e}")

    def poll(self, timeout_ms: int = 100):
        if self.bpf:
            self.bpf.perf_buffer_poll(timeout=timeout_ms)

    def get_stack(self, stack_id: int) -> Optional[List[int]]:
        if self.bpf is None or stack_id < 0:
            return None
        try:
            return list(self.bpf["stack_traces"].walk(stack_id))
        except Exception:
            return None

    def get_stack_symbol(self, addr: int) -> str:
        if self.bpf is None:
            return f"0x{addr:x}"
        try:
            return self.bpf.sym(addr, self.config.pid, show_offset=True)
        except Exception:
            return f"0x{addr:x}"

    def stop(self):
        if self.bpf:
            self.bpf.cleanup()
            self.bpf = None
        self._dispatch_entry_prog_loaded = False
        logger.info("Tracer stopped")
