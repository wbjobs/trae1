#!/usr/bin/env python3
"""
Container Escape Detection Tool (Python + BCC/eBPF)

Runs on the host and hooks key kernel functions to detect container escape
behavior such as:
  * Mounting host procfs/sysfs into a container
  * Using name_to_handle_at / open_by_handle_at to reference host files
  * Modifying namespaces via unshare / setns
  * Privilege-escalating mount flags / namespaces changes

Events are logged to syslog (and optionally stdout). A whitelist file allows
excluding known-legitimate operations.

Usage:
  sudo python3 container_escape_detector.py                         \
      [--monitor-containers <id1,id2>]                              \
      [--whitelist /etc/container-escape-detector/whitelist.yaml]   \
      [--verbose]
"""

from __future__ import annotations

import argparse
import ctypes as ct
import json
import math
import os
import re
import signal
import sys
import syslog
import threading
import time
from collections import defaultdict, deque
from http.server import HTTPServer, BaseHTTPRequestHandler
from typing import Any, Deque, Dict, List, Optional, Set, Tuple
from urllib.parse import urlparse, parse_qs

try:
    from bcc import BPF
except ImportError:  # pragma: no cover - import error is user-facing
    sys.stderr.write(
        "[ERROR] bcc Python package is required. Install with:\n"
        "    sudo apt-get install python3-bpfcc bpfcc-tools linux-headers-$(uname -r)\n"
    )
    sys.exit(1)


# ---------------------------------------------------------------------------
# eBPF program
# ---------------------------------------------------------------------------
BPF_PROGRAM = r"""
#include <uapi/linux/ptrace.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <linux/pid_namespace.h>
#include <linux/fs_struct.h>

#include <linux/cred.h>

#define CE_TYPE_MOUNT_ENTRY      1
#define CE_TYPE_MOUNT_RET        2
#define CE_TYPE_UNSHARE_ENTRY    3
#define CE_TYPE_UNSHARE_RET      4
#define CE_TYPE_SETNS_ENTRY      5
#define CE_TYPE_SETNS_RET        6
#define CE_TYPE_OPEN_HANDLE      7
#define CE_TYPE_NAME_TO_HANDLE   8

#define TASK_COMM_LEN 16
#define MOUNT_SOURCE_LEN 64
#define MOUNT_TARGET_LEN 128
#define MOUNT_FSTYPE_LEN 32
#define MOUNT_DATA_LEN 128

struct event {
    u64 ts_ns;
    u32 pid;
    u32 tgid;
    u32 uid;
    u32 gid;
    u32 real_uid;
    u32 real_gid;
    u32 ppid;
    char comm[TASK_COMM_LEN];
    u8  type;
    s32 ret;
    u32 flags;          /* mount flags or unshare flags          */
    char source[MOUNT_SOURCE_LEN];
    char target[MOUNT_TARGET_LEN];
    char fstype[MOUNT_FSTYPE_LEN];
    char data[MOUNT_DATA_LEN];
    u32 ns_fd;          /* setns target fd                      */
    u32 ns_type;        /* setns ns_type / unshare flags hint   */
};

BPF_PERF_OUTPUT(events);
BPF_HASH(tmp_mount_args, u64, struct event);
BPF_HASH(tmp_unshare_args, u64, u32);
BPF_HASH(tmp_setns_args, u64, struct event);

static __always_inline void fill_common(struct event *e, u8 type) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    e->type = type;
    e->pid    = pid_tgid & 0xffffffff;
    e->tgid   = pid_tgid >> 32;
    e->uid    = bpf_get_current_uid_gid() & 0xffffffff;
    e->gid    = bpf_get_current_uid_gid() >> 32;

    /* real_cred gives the host-namespace credentials even when the process
     * is inside a user namespace.  bpf_get_current_uid_gid() returns the
     * *current* (i.e. userns-mapped) uid/gid, which is often 0 inside
     * containers even when the real host user is unprivileged. */
    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    const struct cred *real_cred = NULL;
    bpf_probe_read_kernel(&real_cred, sizeof(real_cred), &task->real_cred);
    if (real_cred) {
        bpf_probe_read_kernel(&e->real_uid, sizeof(e->real_uid), &real_cred->uid);
        bpf_probe_read_kernel(&e->real_gid, sizeof(e->real_gid), &real_cred->gid);
    } else {
        e->real_uid = (u32)-1;
        e->real_gid = (u32)-1;
    }

    e->ppid   = 0;
    e->ts_ns  = bpf_ktime_get_ns();
    bpf_get_current_comm(&e->comm, sizeof(e->comm));
    e->ret    = 0;
    e->flags  = 0;
    e->ns_fd  = 0;
    e->ns_type = 0;
    __builtin_memset(e->source, 0, sizeof(e->source));
    __builtin_memset(e->target, 0, sizeof(e->target));
    __builtin_memset(e->fstype, 0, sizeof(e->fstype));
    __builtin_memset(e->data,   0, sizeof(e->data));
}

/* ------------------------------------------------------------------ mount */
TRACEPOINT_PROBE(syscalls, sys_enter_mount) {
    u64 id = bpf_get_current_pid_tgid();
    struct event e = {};
    fill_common(&e, CE_TYPE_MOUNT_ENTRY);

    e.flags = (u32)args->flags;
    bpf_probe_read_user_str(&e.source, sizeof(e.source), (void *)args->dev_name);
    bpf_probe_read_user_str(&e.target, sizeof(e.target), (void *)args->dir_name);
    bpf_probe_read_user_str(&e.fstype, sizeof(e.fstype), (void *)args->type);
    if (args->data_page) {
        bpf_probe_read_user_str(&e.data, sizeof(e.data), (void *)args->data_page);
    }
    tmp_mount_args.update(&id, &e);
    return 0;
}

TRACEPOINT_PROBE(syscalls, sys_exit_mount) {
    u64 id = bpf_get_current_pid_tgid();
    struct event *ep = tmp_mount_args.lookup(&id);
    if (!ep) return 0;
    ep->ret = args->ret;
    ep->type = CE_TYPE_MOUNT_RET;
    events.perf_submit(args, ep, sizeof(*ep));
    tmp_mount_args.delete(&id);
    return 0;
}

/* ---------------------------------------------------------------- unshare */
TRACEPOINT_PROBE(syscalls, sys_enter_unshare) {
    u64 id = bpf_get_current_pid_tgid();
    u32 flags = (u32)args->flags;
    tmp_unshare_args.update(&id, &flags);
    return 0;
}

TRACEPOINT_PROBE(syscalls, sys_exit_unshare) {
    u64 id = bpf_get_current_pid_tgid();
    u32 *fp = tmp_unshare_args.lookup(&id);
    if (!fp) return 0;
    struct event e = {};
    fill_common(&e, CE_TYPE_UNSHARE_RET);
    e.flags   = *fp;
    e.ns_type = *fp;
    e.ret     = args->ret;
    events.perf_submit(args, &e, sizeof(e));
    tmp_unshare_args.delete(&id);
    return 0;
}

/* ------------------------------------------------------------------- setns */
TRACEPOINT_PROBE(syscalls, sys_enter_setns) {
    u64 id = bpf_get_current_pid_tgid();
    struct event e = {};
    fill_common(&e, CE_TYPE_SETNS_ENTRY);
    e.ns_fd   = args->fd;
    e.ns_type = args->nstype;
    tmp_setns_args.update(&id, &e);
    return 0;
}

TRACEPOINT_PROBE(syscalls, sys_exit_setns) {
    u64 id = bpf_get_current_pid_tgid();
    struct event *ep = tmp_setns_args.lookup(&id);
    if (!ep) return 0;
    ep->ret  = args->ret;
    ep->type = CE_TYPE_SETNS_RET;
    events.perf_submit(args, ep, sizeof(*ep));
    tmp_setns_args.delete(&id);
    return 0;
}

/* ----------------------------------------------- open_by_handle_at */
TRACEPOINT_PROBE(syscalls, sys_enter_open_by_handle_at) {
    struct event e = {};
    fill_common(&e, CE_TYPE_OPEN_HANDLE);
    e.flags = (u32)args->flags;
    events.perf_submit(args, &e, sizeof(e));
    return 0;
}

/* ----------------------------------------------- name_to_handle_at */
TRACEPOINT_PROBE(syscalls, sys_enter_name_to_handle_at) {
    struct event e = {};
    fill_common(&e, CE_TYPE_NAME_TO_HANDLE);
    events.perf_submit(args, &e, sizeof(e));
    return 0;
}
"""

# ---------------------------------------------------------------------------
# Event structure (must mirror the BPF `struct event`)
# ---------------------------------------------------------------------------
class Event(ct.Structure):
    _fields_ = [
        ("ts_ns", ct.c_ulonglong),
        ("pid", ct.c_uint),
        ("tgid", ct.c_uint),
        ("uid", ct.c_uint),
        ("gid", ct.c_uint),
        ("real_uid", ct.c_uint),
        ("real_gid", ct.c_uint),
        ("ppid", ct.c_uint),
        ("comm", ct.c_char * 16),
        ("type", ct.c_ubyte),
        ("ret", ct.c_int),
        ("flags", ct.c_uint),
        ("source", ct.c_char * 64),
        ("target", ct.c_char * 128),
        ("fstype", ct.c_char * 32),
        ("data", ct.c_char * 128),
        ("ns_fd", ct.c_uint),
        ("ns_type", ct.c_uint),
    ]


EVENT_TYPE_NAME = {
    1: "MOUNT_ENTRY",
    2: "MOUNT",
    3: "UNSHARE_ENTRY",
    4: "UNSHARE",
    5: "SETNS_ENTRY",
    6: "SETNS",
    7: "OPEN_BY_HANDLE_AT",
    8: "NAME_TO_HANDLE_AT",
}


# ---------------------------------------------------------------------------
# Utility helpers
# ---------------------------------------------------------------------------
def parse_cgroup_container_id(pid: int) -> Optional[str]:
    """
    Read /proc/<pid>/cgroup and try to extract a container identifier.
    Supports Docker, containerd (k8s), Podman, LXC patterns.
    """
    path = f"/proc/{pid}/cgroup"
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
    except FileNotFoundError:
        return None
    except PermissionError:
        return None

    # cgroup v2 single line: 0::/system.slice/docker-<id>.scope
    # cgroup v1 multi-line: ... /docker/<id> or /kubepods/.../<id>
    patterns = [
        r"docker-([a-f0-9]{64})\.scope",
        r"containerd-([a-f0-9]{64})\.scope",
        r"cri-containerd-([a-f0-9]{64})\.scope",
        r"/docker/([a-f0-9]{64})",
        r"/kubepods/.*/([a-f0-9]{64})",
        r"/libpod-([a-f0-9]{64})",
        r"/lxc/([a-f0-9]{16,64})",
        r"machine-rkt\\x2d([a-f0-9-]+)\.scope",
    ]
    for pat in patterns:
        m = re.search(pat, content)
        if m:
            return m.group(1)
    return None


def proc_cmdline(pid: int) -> str:
    path = f"/proc/{pid}/cmdline"
    try:
        with open(path, "rb") as f:
            raw = f.read().replace(b"\x00", b" ").strip()
            return raw.decode("utf-8", errors="ignore")
    except Exception:
        return ""


def proc_ppid(pid: int) -> int:
    try:
        with open(f"/proc/{pid}/status", "r", encoding="utf-8") as f:
            for line in f:
                if line.startswith("PPid:"):
                    return int(line.split()[1])
    except Exception:
        pass
    return 0


# ---------------------------------------------------------------------------
# User-namespace UID/GID mapping resolver
# ---------------------------------------------------------------------------
class UidGidMapper:
    """
    Parse /proc/<pid>/uid_map and /proc/<pid>/gid_map to translate between
    container-internal IDs and host IDs.

    The kernel uid_map file format is one line per range:
        <container_id> <host_id> <range_length>

    Example (container root 0 maps to host 1000):
        0 1000 1
        1 100000 65536

    The mapper caches the parsed map per pid for the lifetime of the
    detector process; since containers rarely change their userns mapping
    after start, this is safe.
    """

    # Cache keyed by pid -> (uid_map_list, gid_map_list, inode_key)
    # Each map is a list of (container_start, host_start, length) tuples.
    _cache: Dict[int, Tuple[List[Tuple[int, int, int]],
                            List[Tuple[int, int, int]],
                            int]] = {}

    @classmethod
    def clear(cls) -> None:
        cls._cache.clear()

    @classmethod
    def _read_map(cls, path: str) -> List[Tuple[int, int, int]]:
        ranges: List[Tuple[int, int, int]] = []
        try:
            with open(path, "r", encoding="utf-8") as f:
                for line in f:
                    parts = line.strip().split()
                    if len(parts) >= 3:
                        try:
                            ranges.append((int(parts[0]), int(parts[1]), int(parts[2])))
                        except ValueError:
                            continue
        except (FileNotFoundError, PermissionError):
            pass
        return ranges

    @classmethod
    def _userns_inode(cls, pid: int) -> int:
        try:
            return os.stat(f"/proc/{pid}/ns/user").st_ino
        except Exception:
            return -1

    @classmethod
    def _get_maps(cls, pid: int) -> Tuple[List[Tuple[int, int, int]],
                                           List[Tuple[int, int, int]]]:
        inode = cls._userns_inode(pid)
        cached = cls._cache.get(pid)
        if cached is not None and cached[2] == inode:
            return cached[0], cached[1]

        uid_map = cls._read_map(f"/proc/{pid}/uid_map")
        gid_map = cls._read_map(f"/proc/{pid}/gid_map")
        cls._cache[pid] = (uid_map, gid_map, inode)
        return uid_map, gid_map

    @classmethod
    def _translate(cls, id_value: int,
                   ranges: List[Tuple[int, int, int]]) -> Optional[int]:
        for container_start, host_start, length in ranges:
            if container_start <= id_value < container_start + length:
                return host_start + (id_value - container_start)
        return None

    @classmethod
    def uid_to_host(cls, pid: int, uid: int) -> Optional[int]:
        uid_map, _ = cls._get_maps(pid)
        return cls._translate(uid, uid_map)

    @classmethod
    def gid_to_host(cls, pid: int, gid: int) -> Optional[int]:
        _, gid_map = cls._get_maps(pid)
        return cls._translate(gid, gid_map)

    @classmethod
    def has_mapping(cls, pid: int) -> bool:
        """Return True if the process is inside a user namespace with a
        mapping that differs from the host identity map."""
        uid_map, gid_map = cls._get_maps(pid)
        if not uid_map and not gid_map:
            return False
        for ranges in (uid_map, gid_map):
            for container_start, host_start, length in ranges:
                if container_start != host_start or length != 4294967295:
                    return True
        return False


# ---------------------------------------------------------------------------
# Detection logic
# ---------------------------------------------------------------------------
# Mount flags we consider suspicious inside containers
MS_BIND    = 1 << 0
MS_SHARED  = 1 << 16

# Namespace flags (clone / unshare)
CLONE_NEWNS    = 0x00020000
CLONE_NEWUTS   = 0x04000000
CLONE_NEWIPC   = 0x08000000
CLONE_NEWUSER  = 0x10000000
CLONE_NEWPID   = 0x20000000
CLONE_NEWNET   = 0x40000000
CLONE_NEWCGROUP = 0x02000000
CLONE_NEWTIME  = 0x00000080


def classify_event(e: Event,
                   container_id: Optional[str],
                   cmdline: str) -> Tuple[bool, List[str]]:
    """
    Return (is_suspicious, reasons).
    Only events originating from inside a container (container_id != None)
    are considered here; host processes are ignored unless they try to do
    something container-related.
    """
    reasons: List[str] = []
    etype = e.type

    # --- open_by_handle_at / name_to_handle_at ---
    if etype in (7, 8):
        reasons.append(f"{EVENT_TYPE_NAME[etype]}: process attempted to use file handle API "
                       f"which may reference host files")

    # --- setns ---
    if etype == 6:
        if e.ns_type != 0:
            reasons.append(
                f"SETNS: attempt to attach to namespace nstype={e.ns_type} fd={e.ns_fd} ret={e.ret}"
            )
        else:
            reasons.append(
                f"SETNS: attempt to derive namespace type from fd={e.ns_fd} ret={e.ret}"
            )

    # --- unshare ---
    if etype == 4:
        flags = e.ns_type or e.flags
        interesting = []
        if flags & CLONE_NEWNS:      interesting.append("NEWNS")
        if flags & CLONE_NEWUTS:     interesting.append("NEWUTS")
        if flags & CLONE_NEWIPC:     interesting.append("NEWIPC")
        if flags & CLONE_NEWUSER:    interesting.append("NEWUSER")
        if flags & CLONE_NEWPID:     interesting.append("NEWPID")
        if flags & CLONE_NEWNET:     interesting.append("NEWNET")
        if flags & CLONE_NEWCGROUP:  interesting.append("NEWCGROUP")
        if flags & CLONE_NEWTIME:    interesting.append("NEWTIME")
        if interesting and e.ret == 0:
            reasons.append(f"UNSHARE: namespaces modified: {','.join(interesting)}")
        elif interesting:
            reasons.append(
                f"UNSHARE: attempt to modify namespaces {','.join(interesting)} (ret={e.ret})"
            )

    # --- mount ---
    if etype == 2:
        target = (e.target or b"").decode("utf-8", errors="ignore")
        fstype = (e.fstype or b"").decode("utf-8", errors="ignore")
        source = (e.source or b"").decode("utf-8", errors="ignore")
        data   = (e.data or b"").decode("utf-8", errors="ignore")
        flags  = e.flags

        if fstype == "proc":
            reasons.append(f"MOUNT procfs on '{target}' (ret={e.ret})")
        if fstype == "sysfs":
            reasons.append(f"MOUNT sysfs on '{target}' (ret={e.ret})")
        if fstype == "cgroup" or fstype == "cgroup2":
            reasons.append(f"MOUNT cgroupfs ({fstype}) on '{target}' (ret={e.ret})")
        if fstype == "devpts" and target == "/dev/pts":
            reasons.append(f"MOUNT devpts on '{target}' (ret={e.ret})")
        if flags & MS_SHARED:
            reasons.append(f"MOUNT uses MS_SHARED on '{target}' (propagation escape risk)")
        if flags & MS_BIND:
            reasons.append(f"MOUNT bind-mount source='{source}' onto '{target}' (ret={e.ret})")
        if "proc" in data.lower() or "proc" in target.lower():
            if fstype and fstype != "proc":
                reasons.append(
                    f"MOUNT fstype={fstype} target='{target}' data='{data}' mentions proc"
                )

    return (len(reasons) > 0, reasons)


# ---------------------------------------------------------------------------
# Whitelist
# ---------------------------------------------------------------------------
class Whitelist:
    """Simple YAML/JSON whitelist loader."""

    def __init__(self, path: Optional[str]):
        self.path = path
        self.container_ids: Set[str] = set()
        self.commands: List[re.Pattern] = []
        self.event_types: Set[str] = set()
        if path:
            self._load(path)

    def _load(self, path: str) -> None:
        try:
            import yaml  # type: ignore
            with open(path, "r", encoding="utf-8") as f:
                data = yaml.safe_load(f) or {}
        except ImportError:
            # Fallback: try JSON
            try:
                with open(path, "r", encoding="utf-8") as f:
                    data = json.load(f)
            except Exception:
                sys.stderr.write(f"[WARN] cannot parse whitelist {path}\n")
                return
        except FileNotFoundError:
            sys.stderr.write(f"[WARN] whitelist file not found: {path}\n")
            return

        self.container_ids = set(data.get("container_ids", []) or [])
        self.event_types   = set(data.get("event_types", []) or [])
        for pat in (data.get("command_patterns", []) or []):
            try:
                self.commands.append(re.compile(pat))
            except re.error as ex:
                sys.stderr.write(f"[WARN] invalid regex in whitelist: {pat}: {ex}\n")

    def is_whitelisted(self,
                       container_id: Optional[str],
                       event_type: str,
                       command: str) -> bool:
        if container_id and container_id in self.container_ids:
            return True
        if event_type in self.event_types:
            return True
        for pat in self.commands:
            if pat.search(command):
                return True
        return False


# ---------------------------------------------------------------------------
# Scoring rules loader
# ---------------------------------------------------------------------------
DEFAULT_SCORING_RULES: Dict[str, Any] = {
    "threshold": 20,
    "time_window": 300,
    "decay_half_life": None,
    "event_scores": {
        "MOUNT": {"default": 5},
        "UNSHARE": {"default": 4},
        "SETNS": {"default": 6},
        "OPEN_BY_HANDLE_AT": {"default": 9},
        "NAME_TO_HANDLE_AT": {"default": 7},
    },
    "correlations": [],
}


def _load_yaml_file(path: str) -> Optional[Dict[str, Any]]:
    try:
        import yaml  # type: ignore
        with open(path, "r", encoding="utf-8") as f:
            data = yaml.safe_load(f) or {}
        return data
    except ImportError:
        try:
            with open(path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            sys.stderr.write(f"[WARN] cannot parse {path} as YAML or JSON\n")
            return None
    except FileNotFoundError:
        sys.stderr.write(f"[WARN] scoring rules file not found: {path}\n")
        return None


# ---------------------------------------------------------------------------
# Behavior correlation and risk scoring
# ---------------------------------------------------------------------------
class BehaviorRecord:
    """A single recorded behavior event for correlation."""

    __slots__ = ("ts", "event_type", "score", "reason", "pid", "comm", "cmdline")

    def __init__(self, ts: float, event_type: str, score: int,
                 reason: str, pid: int, comm: str, cmdline: str):
        self.ts = ts
        self.event_type = event_type
        self.score = score
        self.reason = reason
        self.pid = pid
        self.comm = comm
        self.cmdline = cmdline

    def to_dict(self) -> Dict[str, Any]:
        return {
            "time": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(self.ts)),
            "ts": self.ts,
            "event_type": self.event_type,
            "score": self.score,
            "reason": self.reason,
            "pid": self.pid,
            "comm": self.comm,
            "cmdline": self.cmdline,
        }


class _CorrelationMatch:
    __slots__ = ("name", "bonus", "matched_at", "matched_indices")

    def __init__(self, name: str, bonus: int, matched_at: float,
                 matched_indices: List[int]):
        self.name = name
        self.bonus = bonus
        self.matched_at = matched_at
        self.matched_indices = matched_indices


class BehaviorCorrelator:
    """
    Tracks per-container behavior timelines, scores individual events,
    detects correlation chains, and computes a cumulative risk score.

    Scoring:
      * Each event receives a base score from `event_scores` (matched by
        event type and, if present, the reason tag).
      * Within the configured `time_window`, sequences of events that
        match `correlations` rules add `bonus` points.
      * If `decay_half_life` is set, older event scores decay exponentially.
      * When the total (individual + bonuses) exceeds `threshold`, the
        container is marked HIGH-RISK.

    Thread-safe: all state access is guarded by `_lock`.
    """

    def __init__(self, rules_path: Optional[str] = None):
        self._lock = threading.Lock()
        self._rules_path = rules_path
        self._rules: Dict[str, Any] = dict(DEFAULT_SCORING_RULES)
        self._timelines: Dict[str, Deque[BehaviorRecord]] = defaultdict(
            lambda: deque()
        )
        self._correlation_matches: Dict[str, Deque[_CorrelationMatch]] = defaultdict(
            lambda: deque()
        )
        self._last_correlation_trigger: Dict[str, float] = {}
        self._load_rules()

    # ----- rules management -----
    def _load_rules(self) -> None:
        if self._rules_path:
            data = _load_yaml_file(self._rules_path)
            if data:
                self._apply_rules(data)
                sys.stderr.write(
                    f"[INFO] scoring rules loaded from {self._rules_path}\n"
                )

    def _apply_rules(self, data: Dict[str, Any]) -> None:
        self._rules["threshold"] = int(data.get("threshold", 20))
        self._rules["time_window"] = int(data.get("time_window", 300))
        self._rules["decay_half_life"] = data.get("decay_half_life")
        self._rules["event_scores"] = data.get("event_scores",
                                                self._rules["event_scores"])
        self._rules["correlations"] = data.get("correlations", [])

    def reload(self) -> bool:
        """Hot-reload scoring rules from disk. Returns True on success."""
        with self._lock:
            if not self._rules_path:
                return False
            data = _load_yaml_file(self._rules_path)
            if data:
                self._apply_rules(data)
                return True
            return False

    def get_rules(self) -> Dict[str, Any]:
        with self._lock:
            return json.loads(json.dumps(self._rules))

    # ----- scoring -----
    def _score_for_event(self, event_type: str,
                          reason_tags: List[str]) -> int:
        scores = self._rules["event_scores"].get(event_type, {})
        if isinstance(scores, dict):
            for tag in reason_tags:
                if tag in scores:
                    return int(scores[tag])
            return int(scores.get("default", 1))
        return int(scores)

    @staticmethod
    def _extract_reason_tags(reasons: List[str]) -> List[str]:
        """Extract short tags from classify_event reasons for scoring lookup."""
        tags: List[str] = []
        for r in reasons:
            lower = r.lower()
            if "procfs" in lower:
                tags.append("procfs")
            if "sysfs" in lower:
                tags.append("sysfs")
            if "cgroup" in lower:
                tags.append("cgroupfs")
            if "devpts" in lower:
                tags.append("devpts")
            if "bind-mount" in lower:
                tags.append("bind_mount")
            if "ms_shared" in lower or "shared" in lower:
                tags.append("shared_propagation")
            if "newns" in lower:
                tags.append("NEWNS")
            if "newuser" in lower:
                tags.append("NEWUSER")
            if "newpid" in lower:
                tags.append("NEWPID")
            if "newuts" in lower:
                tags.append("NEWUTS")
            if "newipc" in lower:
                tags.append("NEWIPC")
            if "newnet" in lower:
                tags.append("NEWNET")
            if "newcgroup" in lower:
                tags.append("NEWCGROUP")
            if "newtime" in lower:
                tags.append("NEWTIME")
        return tags

    # ----- correlation detection -----
    def _check_correlations(self, cid: str) -> List[_CorrelationMatch]:
        """Check if recent events match any configured correlation chains."""
        matches: List[_CorrelationMatch] = []
        now = time.time()
        timeline = list(self._timelines[cid])
        correlations = self._rules.get("correlations", [])

        for rule in correlations:
            name = rule.get("name", "unnamed")
            sequence = rule.get("sequence", [])
            window = int(rule.get("window", 60))
            bonus = int(rule.get("bonus", 5))

            last_trigger = self._last_correlation_trigger.get(cid, 0)
            if now - last_trigger < window:
                continue

            seq_len = len(sequence)
            if seq_len == 0 or seq_len > len(timeline):
                continue

            for start in range(len(timeline) - seq_len + 1):
                indices: List[int] = []
                t0 = timeline[start].ts
                ok = True
                for offset, expected_type in enumerate(sequence):
                    idx = start + offset
                    if idx >= len(timeline):
                        ok = False
                        break
                    rec = timeline[idx]
                    if rec.event_type != expected_type:
                        ok = False
                        break
                    if rec.ts - t0 > window:
                        ok = False
                        break
                    indices.append(idx)
                if ok:
                    matches.append(_CorrelationMatch(name, bonus, now, indices))
                    self._last_correlation_trigger[cid] = now
                    break

        return matches

    # ----- record an event -----
    def record_event(self,
                      container_id: str,
                      event_type: str,
                      reasons: List[str],
                      pid: int,
                      comm: str,
                      cmdline: str) -> Tuple[int, bool, List[str]]:
        """
        Record a suspicious event for the given container.

        Returns (risk_score, is_high_risk, correlation_names).
        """
        with self._lock:
            now = time.time()
            window = self._rules["time_window"]
            tags = self._extract_reason_tags(reasons)
            score = self._score_for_event(event_type, tags)

            rec = BehaviorRecord(now, event_type, score,
                                  "; ".join(reasons), pid, comm, cmdline)

            dq = self._timelines[container_id]
            dq.append(rec)

            while dq and (now - dq[0].ts) > window:
                dq.popleft()

            new_matches = self._check_correlations(container_id)
            if new_matches:
                mdq = self._correlation_matches[container_id]
                for m in new_matches:
                    mdq.append(m)
                while mdq and (now - mdq[0].matched_at) > window:
                    mdq.popleft()

            risk_score = self._compute_risk_score_locked(container_id)
            threshold = self._rules["threshold"]
            is_high = risk_score >= threshold
            corr_names = [m.name for m in new_matches]

            return risk_score, is_high, corr_names

    def _compute_risk_score_locked(self, cid: str) -> int:
        """Compute total risk score without acquiring the lock."""
        total = 0
        now = time.time()
        half_life = self._rules.get("decay_half_life")

        for rec in self._timelines[cid]:
            if half_life and half_life > 0:
                age = now - rec.ts
                decay = math.exp(-0.693 * age / half_life)
                total += rec.score * decay
            else:
                total += rec.score

        for m in self._correlation_matches[cid]:
            total += m.bonus

        return int(round(total))

    # ----- query API -----
    def get_all_containers(self) -> List[Dict[str, Any]]:
        with self._lock:
            result = []
            for cid in sorted(self._timelines.keys()):
                score = self._compute_risk_score_locked(cid)
                threshold = self._rules["threshold"]
                event_count = len(self._timelines[cid])
                match_count = len(self._correlation_matches[cid])
                result.append({
                    "container_id": cid,
                    "risk_score": score,
                    "risk_level": "HIGH" if score >= threshold else
                                  ("MEDIUM" if score >= threshold // 2 else "LOW"),
                    "event_count": event_count,
                    "correlation_count": match_count,
                    "threshold": threshold,
                })
            return result

    def get_container_timeline(self, cid: str) -> Optional[Dict[str, Any]]:
        with self._lock:
            if cid not in self._timelines:
                return None
            score = self._compute_risk_score_locked(cid)
            threshold = self._rules["threshold"]
            events = [r.to_dict() for r in self._timelines[cid]]
            corrs = [
                {
                    "name": m.name,
                    "bonus": m.bonus,
                    "matched_at": time.strftime(
                        "%Y-%m-%dT%H:%M:%S", time.localtime(m.matched_at)
                    ),
                }
                for m in self._correlation_matches[cid]
            ]
            return {
                "container_id": cid,
                "risk_score": score,
                "risk_level": "HIGH" if score >= threshold else
                              ("MEDIUM" if score >= threshold // 2 else "LOW"),
                "threshold": threshold,
                "events": events,
                "correlations": corrs,
            }


# ---------------------------------------------------------------------------
# HTTP API server
# ---------------------------------------------------------------------------
class _ApiHandler(BaseHTTPRequestHandler):
    correlator: Optional[BehaviorCorrelator] = None

    def _send_json(self, data: Any, status: int = 200) -> None:
        body = json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_text(self, text: str, status: int = 404) -> None:
        body = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt: str, *args: Any) -> None:  # silence default logs
        return

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/")
        correlator = _ApiHandler.correlator
        if correlator is None:
            self._send_text("correlator not available", 503)
            return

        if path == "/api/containers":
            self._send_json(correlator.get_all_containers())
        elif path.startswith("/api/containers/"):
            cid = path[len("/api/containers/"):]
            tl = correlator.get_container_timeline(cid)
            if tl is None:
                self._send_text(f"container {cid} not found", 404)
            else:
                self._send_json(tl)
        elif path == "/api/scoring/rules":
            self._send_json(correlator.get_rules())
        elif path == "/api/health":
            self._send_json({"status": "ok", "time": time.time()})
        else:
            self._send_text("not found", 404)

    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/")
        correlator = _ApiHandler.correlator
        if correlator is None:
            self._send_text("correlator not available", 503)
            return

        if path == "/api/scoring/reload":
            ok = correlator.reload()
            self._send_json({
                "reloaded": ok,
                "rules": correlator.get_rules(),
            })
        else:
            self._send_text("not found", 404)


class RiskApiServer:
    """Thin wrapper that runs _ApiHandler in a daemon thread."""

    def __init__(self, correlator: BehaviorCorrelator, host: str = "0.0.0.0",
                 port: int = 8765):
        _ApiHandler.correlator = correlator
        self._host = host
        self._port = port
        self._server: Optional[HTTPServer] = None
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        try:
            self._server = HTTPServer((self._host, self._port), _ApiHandler)
        except OSError as ex:
            sys.stderr.write(f"[WARN] cannot start API server on "
                             f"{self._host}:{self._port}: {ex}\n")
            return
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()
        print(f"[INFO] risk API server listening on http://{self._host}:{self._port}")

    def _serve(self) -> None:
        assert self._server is not None
        try:
            self._server.serve_forever()
        except Exception:
            pass

    def stop(self) -> None:
        if self._server:
            self._server.shutdown()
            self._server.server_close()
        if self._thread:
            self._thread.join(timeout=2)


# ---------------------------------------------------------------------------
# Detector
# ---------------------------------------------------------------------------
class ContainerEscapeDetector:
    def __init__(self,
                 monitor_containers: Optional[List[str]] = None,
                 whitelist: Optional[Whitelist] = None,
                 correlator: Optional[BehaviorCorrelator] = None,
                 api_server: Optional[RiskApiServer] = None,
                 verbose: bool = False):
        self.monitor_containers = set(monitor_containers) if monitor_containers else None
        self.whitelist = whitelist or Whitelist(None)
        self.correlator = correlator or BehaviorCorrelator()
        self.api_server = api_server
        self.verbose = verbose
        self._stop = False
        self._seen_ppids: Dict[int, int] = {}
        self._alert_count: Dict[str, int] = defaultdict(int)

        syslog.openlog("container-escape-detector",
                       syslog.LOG_PID | syslog.LOG_NDELAY,
                       syslog.LOG_AUTH)

        self.bpf = BPF(text=BPF_PROGRAM)
        self.bpf["events"].open_perf_buffer(self._on_event, page_cnt=128)

    # ----- event dispatch -----
    def _on_event(self, _cpu, data, _size):
        e = ct.cast(data, ct.POINTER(Event)).contents
        try:
            self._handle_event(e)
        except Exception as ex:  # pragma: no cover - defensive
            if self.verbose:
                sys.stderr.write(f"[DEBUG] event handler error: {ex}\n")

    def _handle_event(self, e: Event) -> None:
        pid = e.tgid
        comm = e.comm.decode("utf-8", errors="ignore")
        cmdline = proc_cmdline(pid)
        cid = parse_cgroup_container_id(pid)

        # If the user asked to monitor specific containers, drop the rest
        if self.monitor_containers is not None:
            if cid is None or cid not in self.monitor_containers:
                return

        # Only consider events from inside a container for escape detection.
        # Host processes are ignored (they are expected to perform mounts /
        # unshare / setns). If the user explicitly specified target containers
        # via --monitor-containers, we have already dropped anything not in
        # that list above.
        if cid is None:
            return

        suspicious, reasons = classify_event(e, cid, cmdline)
        if not suspicious:
            return

        etype_name = EVENT_TYPE_NAME.get(e.type, f"UNKNOWN({e.type})")
        if self.whitelist.is_whitelisted(cid, etype_name, cmdline or comm):
            if self.verbose:
                sys.stderr.write(f"[DEBUG] whitelisted: {etype_name} cid={cid} "
                                 f"cmd={cmdline or comm}\n")
            return

        # Feed into the behavioral correlator for risk scoring and
        # chain detection.
        risk_score, is_high, corr_names = self.correlator.record_event(
            container_id=cid,
            event_type=etype_name,
            reasons=reasons,
            pid=pid,
            comm=comm,
            cmdline=cmdline,
        )

        self._emit_alert(e, cid, cmdline, reasons,
                          risk_score, is_high, corr_names)

    def _emit_alert(self,
                    e: Event,
                    container_id: Optional[str],
                    cmdline: str,
                    reasons: List[str],
                    risk_score: int = 0,
                    is_high_risk: bool = False,
                    corr_names: Optional[List[str]] = None) -> None:
        comm = e.comm.decode("utf-8", errors="ignore")
        target = (e.target or b"").decode("utf-8", errors="ignore")
        fstype = (e.fstype or b"").decode("utf-8", errors="ignore")
        source = (e.source or b"").decode("utf-8", errors="ignore")
        data = (e.data or b"").decode("utf-8", errors="ignore")
        etype_name = EVENT_TYPE_NAME.get(e.type, f"UNKNOWN({e.type})")
        corr_names = corr_names or []

        ppid = self._seen_ppids.get(e.tgid) or proc_ppid(e.tgid)
        self._seen_ppids[e.tgid] = ppid

        # Resolve host UID/GID:
        #   real_uid / real_gid  come from task_struct->real_cred (host-level
        #                         credentials, always correct regardless of
        #                         user namespaces).
        #   uid / gid            come from bpf_get_current_uid_gid() (the
        #                         container-mapped values, often 0 inside a
        #                         container even when the real host user is
        #                         non-root).
        #   uid_map / gid_map    are parsed from /proc/<pid>/{uid,gid}_map so
        #                         we can display the translation explicitly.
        has_userns = UidGidMapper.has_mapping(e.tgid)
        mapped_uid_host = UidGidMapper.uid_to_host(e.tgid, e.uid)
        mapped_gid_host = UidGidMapper.gid_to_host(e.tgid, e.gid)

        # The "authoritative" host uid is real_uid; mapped_uid_host is a
        # cross-check via /proc and may be None if the map was
        # unparseable or 1:1.
        host_uid = e.real_uid if e.real_uid != 0xFFFFFFFF else (mapped_uid_host or e.uid)
        host_gid = e.real_gid if e.real_gid != 0xFFFFFFFF else (mapped_gid_host or e.gid)

        rules = self.correlator.get_rules()
        threshold = rules.get("threshold", 20)

        record = {
            "time": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()),
            "ts_ns": e.ts_ns,
            "event": etype_name,
            "pid": e.tgid,
            "tid": e.pid,
            "ppid": ppid,
            "uid_container": e.uid,
            "gid_container": e.gid,
            "uid_host_real": host_uid,
            "gid_host_real": host_gid,
            "uid_host_via_map": mapped_uid_host,
            "gid_host_via_map": mapped_gid_host,
            "in_user_ns": has_userns,
            "comm": comm,
            "cmdline": cmdline,
            "container_id": container_id,
            "risk_score": risk_score,
            "risk_threshold": threshold,
            "is_high_risk": is_high_risk,
            "correlations_triggered": corr_names,
            "ret": e.ret,
            "flags": e.flags,
            "mount": {
                "source": source,
                "target": target,
                "fstype": fstype,
                "data": data,
            },
            "ns_fd": e.ns_fd,
            "ns_type": e.ns_type,
            "reasons": reasons,
        }

        if has_userns:
            userns_note = (
                f"uid(container={e.uid}->host={host_uid}) "
                f"gid(container={e.gid}->host={host_gid})"
            )
        else:
            userns_note = (
                f"uid={host_uid} gid={host_gid}"
            )

        risk_note = f"risk={risk_score}/{threshold}"
        if is_high_risk:
            risk_note += " [HIGH]"
        if corr_names:
            risk_note += f" chains=[{','.join(corr_names)}]"

        message = (
            f"CONTAINER_ESCAPE_SUSPECTED event={etype_name} "
            f"pid={e.tgid} {userns_note} "
            f"container_id={container_id or 'N/A'} "
            f"comm={comm} cmdline={cmdline!r} "
            f"{risk_note} "
            f"reasons={'; '.join(reasons)}"
        )

        # Rate-limit repetitive alerts per (cid, event, cmdline)
        key = f"{container_id}|{etype_name}|{cmdline}"
        count = self._alert_count[key] + 1
        self._alert_count[key] = count
        if count > 10 and count % 10 != 0:
            return

        log_level = syslog.LOG_ALERT if is_high_risk else syslog.LOG_WARNING
        syslog.syslog(log_level, message)
        if self.verbose or container_id is None:
            print(f"[ALERT] {message}")
            if self.verbose:
                print("   json:", json.dumps(record, ensure_ascii=False))

    # ----- lifecycle -----
    def run(self) -> None:
        print("[INFO] container-escape-detector: attaching eBPF hooks ...")
        print("[INFO] monitoring target containers: "
              + (", ".join(self.monitor_containers)
                 if self.monitor_containers else "ALL"))
        if self.whitelist.path:
            print(f"[INFO] whitelist loaded from {self.whitelist.path}")
        if self.api_server:
            self.api_server.start()

        def _on_sig(_signum, _frame):
            self._stop = True

        signal.signal(signal.SIGINT, _on_sig)
        signal.signal(signal.SIGTERM, _on_sig)

        try:
            while not self._stop:
                try:
                    self.bpf.perf_buffer_poll(timeout=500)
                except KeyboardInterrupt:
                    break
        finally:
            if self.api_server:
                self.api_server.stop()
        print("\n[INFO] stopping detector.")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def build_argparser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Container escape detection tool (eBPF/BCC)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--monitor-containers",
                   help="Comma-separated list of container IDs to monitor "
                        "(default: all containers)")
    p.add_argument("--whitelist",
                   help="Path to whitelist config (YAML or JSON)")
    p.add_argument("--scoring-rules",
                   help="Path to scoring rules YAML config "
                        "(default: built-in defaults)")
    p.add_argument("--api-host", default="0.0.0.0",
                   help="Host for the risk API server (default: 0.0.0.0)")
    p.add_argument("--api-port", type=int, default=8765,
                   help="Port for the risk API server (default: 8765). "
                        "Set to 0 to disable.")
    p.add_argument("--verbose", "-v", action="store_true",
                   help="Print every alert to stdout and include JSON dump")
    p.add_argument("--dry-run", action="store_true",
                   help="Validate configuration and exit without attaching probes")
    return p


def main(argv: Optional[List[str]] = None) -> int:
    args = build_argparser().parse_args(argv)

    if os.geteuid() != 0:
        sys.stderr.write("[ERROR] this program must be run as root (required for BPF)\n")
        return 2

    monitor = None
    if args.monitor_containers:
        monitor = [c.strip() for c in args.monitor_containers.split(",") if c.strip()]

    wl = Whitelist(args.whitelist)
    correlator = BehaviorCorrelator(args.scoring_rules)

    if args.dry_run:
        print("[OK] configuration valid.")
        print(f"     monitor containers : {monitor or 'ALL'}")
        print(f"     whitelist path     : {args.whitelist or '<none>'}")
        print(f"     whitelist cids     : {wl.container_ids}")
        print(f"     whitelist events   : {wl.event_types}")
        print(f"     scoring rules path : {args.scoring_rules or '<defaults>'}")
        rules = correlator.get_rules()
        print(f"     threshold          : {rules['threshold']}")
        print(f"     time_window        : {rules['time_window']}")
        print(f"     correlations       : {len(rules['correlations'])} rule(s)")
        return 0

    api_server: Optional[RiskApiServer] = None
    if args.api_port > 0:
        api_server = RiskApiServer(correlator,
                                   host=args.api_host,
                                   port=args.api_port)

    detector = ContainerEscapeDetector(
        monitor_containers=monitor,
        whitelist=wl,
        correlator=correlator,
        api_server=api_server,
        verbose=args.verbose,
    )
    detector.run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
