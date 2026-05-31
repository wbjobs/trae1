"""High availability (active-standby) mode implementation"""
import os
import time
import socket
import threading
import logging
from enum import Enum
from typing import Optional, Callable
from dataclasses import dataclass

logger = logging.getLogger(__name__)


class NodeState(Enum):
    INITIALIZING = "initializing"
    ACTIVE = "active"
    STANDBY = "standby"
    FAULT = "fault"
    UNKNOWN = "unknown"


@dataclass
class HAConfig:
    mode: str = "active_standby"
    health_check_interval: int = 5
    election_timeout: int = 10
    shared_lock_path: str = "/tmp/gateway_lock"


class LockFile:
    def __init__(self, path: str):
        self.path = path
        self._lock_fd = None

    def acquire(self, timeout: float = 10.0) -> bool:
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                self._lock_fd = open(self.path, 'w')
                return True
            except (IOError, OSError):
                time.sleep(0.1)
        return False

    def release(self) -> None:
        if self._lock_fd:
            try:
                self._lock_fd.close()
                if os.path.exists(self.path):
                    os.remove(self.path)
            except Exception:
                pass
            self._lock_fd = None

    def is_locked(self) -> bool:
        return os.path.exists(self.path)


class HealthChecker:
    def __init__(self, check_interval: int = 5):
        self._interval = check_interval
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._callbacks: list = []
        self._last_health_check = time.time()
        self._is_healthy = True

    def register_callback(self, callback: Callable[[bool], None]) -> None:
        self._callbacks.append(callback)

    def start(self) -> None:
        self._running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=5)
            self._thread = None

    def _run(self) -> None:
        while self._running:
            self._perform_check()
            time.sleep(self._interval)

    def _perform_check(self) -> None:
        self._last_health_check = time.time()
        is_healthy = self._check_health()

        if is_healthy != self._is_healthy:
            self._is_healthy = is_healthy
            for callback in self._callbacks:
                try:
                    callback(is_healthy)
                except Exception as e:
                    logger.error(f"Health check callback failed: {e}")

    def _check_health(self) -> bool:
        try:
            return True
        except Exception:
            return False

    def get_last_check_time(self) -> float:
        return self._last_health_check

    def is_healthy(self) -> bool:
        return self._is_healthy


class HighAvailabilityManager:
    def __init__(self, config: HAConfig, node_id: str):
        self.config = config
        self.node_id = node_id
        self._state = NodeState.INITIALIZING
        self._lock = LockFile(config.shared_lock_path)
        self._health_checker = HealthChecker(config.health_check_interval)
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._state_change_callbacks: list = []
        self._active_callbacks: list = []
        self._standby_callbacks: list = []
        self._start_time = time.time()

        self._health_checker.register_callback(self._on_health_changed)

    def register_state_change_callback(self, callback: Callable[[NodeState, NodeState], None]) -> None:
        self._state_change_callbacks.append(callback)

    def register_active_callback(self, callback: Callable) -> None:
        self._active_callbacks.append(callback)

    def register_standby_callback(self, callback: Callable) -> None:
        self._standby_callbacks.append(callback)

    def _on_health_changed(self, is_healthy: bool) -> None:
        if not is_healthy and self._state == NodeState.ACTIVE:
            self._transition_to(NodeState.FAULT)

    def _transition_to(self, new_state: NodeState) -> None:
        old_state = self._state
        self._state = new_state

        logger.info(f"Node {self.node_id} state transition: {old_state.value} -> {new_state.value}")

        for callback in self._state_change_callbacks:
            try:
                callback(old_state, new_state)
            except Exception as e:
                logger.error(f"State change callback failed: {e}")

        if new_state == NodeState.ACTIVE:
            for callback in self._active_callbacks:
                try:
                    callback()
                except Exception as e:
                    logger.error(f"Active callback failed: {e}")
        elif new_state == NodeState.STANDBY:
            for callback in self._standby_callbacks:
                try:
                    callback()
                except Exception as e:
                    logger.error(f"Standby callback failed: {e}")

    def _election_loop(self) -> None:
        while self._running:
            if self._state == NodeState.INITIALIZING or self._state == NodeState.FAULT:
                self._try_become_active()

            time.sleep(self.config.election_timeout)

    def _try_become_active(self) -> None:
        if self._lock.acquire(timeout=2.0):
            self._transition_to(NodeState.ACTIVE)
        else:
            if self._state != NodeState.STANDBY:
                self._transition_to(NodeState.STANDBY)

    def start(self) -> None:
        self._running = True
        self._health_checker.start()
        self._thread = threading.Thread(target=self._election_loop, daemon=True)
        self._thread.start()
        self._transition_to(NodeState.INITIALIZING)
        logger.info(f"HA Manager started for node {self.node_id}")

    def stop(self) -> None:
        self._running = False
        self._health_checker.stop()

        if self._state == NodeState.ACTIVE:
            self._lock.release()

        if self._thread:
            self._thread.join(timeout=5)
            self._thread = None

        self._transition_to(NodeState.UNKNOWN)
        logger.info(f"HA Manager stopped for node {self.node_id}")

    def get_state(self) -> NodeState:
        return self._state

    def is_active(self) -> bool:
        return self._state == NodeState.ACTIVE

    def is_standby(self) -> bool:
        return self._state == NodeState.STANDBY

    def force_active(self) -> bool:
        if self._state == NodeState.ACTIVE:
            return True

        if self._lock.acquire(timeout=5.0):
            self._transition_to(NodeState.ACTIVE)
            return True
        return False

    def demote_to_standby(self) -> None:
        if self._state == NodeState.ACTIVE:
            self._lock.release()
            self._transition_to(NodeState.STANDBY)

    def get_uptime(self) -> float:
        return time.time() - self._start_time

    def get_info(self) -> dict:
        return {
            "node_id": self.node_id,
            "state": self._state.value,
            "is_active": self.is_active(),
            "is_standby": self.is_standby(),
            "uptime": self.get_uptime(),
            "health_check_interval": self.config.health_check_interval,
            "election_timeout": self.config.election_timeout,
            "last_health_check": self._health_checker.get_last_check_time(),
            "is_healthy": self._health_checker.is_healthy()
        }


class HACoordinator:
    def __init__(self):
        self._nodes: dict = {}
        self._lock = threading.Lock()

    def register_node(self, node_id: str, ha_manager: HighAvailabilityManager) -> None:
        with self._lock:
            self._nodes[node_id] = ha_manager

    def unregister_node(self, node_id: str) -> None:
        with self._lock:
            if node_id in self._nodes:
                del self._nodes[node_id]

    def get_active_nodes(self) -> list:
        with self._lock:
            return [
                node_id for node_id, ha in self._nodes.items()
                if ha.is_active()
            ]

    def get_standby_nodes(self) -> list:
        with self._lock:
            return [
                node_id for node_id, ha in self._nodes.items()
                if ha.is_standby()
            ]

    def get_cluster_info(self) -> dict:
        with self._lock:
            return {
                "total_nodes": len(self._nodes),
                "active_nodes": len(self.get_active_nodes()),
                "standby_nodes": len(self.get_standby_nodes()),
                "nodes": {
                    node_id: ha.get_info()
                    for node_id, ha in self._nodes.items()
                }
            }
