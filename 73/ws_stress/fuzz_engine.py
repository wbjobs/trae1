"""Fuzzing 测试引擎 - 发送畸形消息 + 检测异常响应 + 统计结果"""

import asyncio
import json
import logging
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional

import aiohttp

from .fuzz_payloads import FuzzPayload, get_all_payloads, load_custom_payloads

logger = logging.getLogger("ws_stress.fuzz")

ANOMALY_CRASH = "crash"
ANOMALY_5XX = "server_error_5xx"
ANOMALY_DISCONNECT = "connection_disconnect"
ANOMALY_TIMEOUT = "response_timeout"
ANOMALY_PROTOCOL_ERROR = "protocol_error"
ANOMALY_MEMORY_HINT = "memory_anomaly_hint"
ANOMALY_NONE = "no_anomaly"


@dataclass
class FuzzResult:
    payload_id: str
    payload_name: str
    payload_category: str
    payload_severity: str
    anomaly_type: str = ANOMALY_NONE
    anomaly_detected: bool = False
    response_code: int = 0
    response_data: str = ""
    connect_disconnected: bool = False
    latency_ms: float = 0.0
    replay_steps: List[str] = field(default_factory=list)
    timestamp: float = 0.0
    raw_response: str = ""
    error_message: str = ""


@dataclass
class FuzzReport:
    config: dict = field(default_factory=dict)
    total_payloads: int = 0
    anomalies_detected: int = 0
    clean_payloads: int = 0
    anomaly_counts: Dict[str, int] = field(default_factory=dict)
    category_counts: Dict[str, int] = field(default_factory=dict)
    severity_counts: Dict[str, int] = field(default_factory=dict)
    results: List[FuzzResult] = field(default_factory=list)
    start_time: float = 0.0
    end_time: float = 0.0
    target_url: str = ""
    node_id: str = ""


class FuzzEngine:
    def __init__(self, url: str, room: str = "fuzz-room",
                 client_id: str = "fuzz-client-001",
                 timeout: float = 10.0,
                 delay_between: float = 0.5):
        self._url = url
        self._room = room
        self._client_id = client_id
        self._timeout = timeout
        self._delay = delay_between
        self._session: Optional[aiohttp.ClientSession] = None
        self._ws: Optional[aiohttp.ClientWebSocketResponse] = None
        self._results: List[FuzzResult] = []
        self._anomaly_counts: Dict[str, int] = {}
        self._category_counts: Dict[str, int] = {}
        self._severity_counts: Dict[str, int] = {}

    async def connect(self) -> bool:
        try:
            if self._session is None or self._session.closed:
                self._session = aiohttp.ClientSession()
            self._ws = await self._session.ws_connect(
                self._url,
                timeout=aiohttp.ClientTimeout(total=self._timeout),
                heartbeat=30,
                max_msg_size=4 * 1024 * 1024,
            )
            join_msg = json.dumps({
                "type": "join",
                "room": self._room,
                "client_id": self._client_id,
            })
            await self._ws.send_str(join_msg)
            return True
        except Exception as e:
            logger.error(f"Fuzz client connect failed: {e}")
            return False

    async def _reconnect_if_needed(self) -> bool:
        if self._ws is None or self._ws.closed:
            return await self.connect()
        return True

    async def _send_and_observe(self, payload: FuzzPayload) -> FuzzResult:
        result = FuzzResult(
            payload_id=payload.id,
            payload_name=payload.name,
            payload_category=payload.category,
            payload_severity=payload.severity,
            timestamp=time.time(),
        )

        try:
            if not await self._reconnect_if_needed():
                result.anomaly_type = ANOMALY_DISCONNECT
                result.anomaly_detected = True
                result.error_message = "Cannot reconnect to server"
                return result

            data = payload.generate()
            start = time.time()

            if isinstance(data, bytes):
                await self._ws.send_bytes(data)
            else:
                await self._ws.send_str(str(data))

            try:
                msg = await asyncio.wait_for(
                    self._ws.receive(),
                    timeout=self._timeout,
                )

                elapsed = (time.time() - start) * 1000
                result.latency_ms = round(elapsed, 2)

                if msg.type == aiohttp.WSMsgType.TEXT:
                    result.raw_response = msg.data[:500] if msg.data else ""
                    try:
                        resp_json = json.loads(msg.data)
                        status = resp_json.get("status", 0)
                        if isinstance(status, int) and status >= 500:
                            result.anomaly_type = ANOMALY_5XX
                            result.anomaly_detected = True
                            result.response_code = status
                        elif isinstance(status, int) and status >= 400:
                            result.anomaly_type = ANOMALY_PROTOCOL_ERROR
                            result.anomaly_detected = True
                            result.response_code = status
                        error_field = resp_json.get("error", "")
                        if error_field:
                            result.error_message = str(error_field)[:200]
                            result.anomaly_type = ANOMALY_PROTOCOL_ERROR
                            result.anomaly_detected = True
                    except json.JSONDecodeError:
                        if "error" in msg.data.lower() or "exception" in msg.data.lower():
                            result.anomaly_type = ANOMALY_PROTOCOL_ERROR
                            result.anomaly_detected = True
                            result.error_message = msg.data[:200]

                elif msg.type == aiohttp.WSMsgType.BINARY:
                    result.raw_response = f"<binary {len(msg.data)} bytes>"

                elif msg.type in (aiohttp.WSMsgType.CLOSE, aiohttp.WSMsgType.CLOSED,
                                   aiohttp.WSMsgType.ERROR):
                    result.anomaly_type = ANOMALY_DISCONNECT
                    result.anomaly_detected = True
                    result.connect_disconnected = True
                    result.error_message = f"Server closed connection (type={msg.type})"

            except asyncio.TimeoutError:
                result.anomaly_type = ANOMALY_TIMEOUT
                result.anomaly_detected = True
                result.error_message = "No response within timeout"

            except aiohttp.ClientWebSocketError as e:
                result.anomaly_type = ANOMALY_DISCONNECT
                result.anomaly_detected = True
                result.connect_disconnected = True
                result.error_message = str(e)[:200]

        except Exception as e:
            result.anomaly_type = ANOMALY_PROTOCOL_ERROR
            result.anomaly_detected = True
            result.error_message = str(e)[:200]

        return result

    async def run(self, payloads: Optional[List[FuzzPayload]] = None,
                  categories: Optional[List[str]] = None,
                  severities: Optional[List[str]] = None,
                  custom_file: Optional[str] = None) -> FuzzReport:
        if payloads is None:
            payloads = get_all_payloads()

        if custom_file:
            custom_payloads = load_custom_payloads(custom_file)
            if custom_payloads:
                logger.info(f"Loaded {len(custom_payloads)} custom payloads from {custom_file}")
                payloads = list(payloads) + custom_payloads

        if categories:
            payloads = [p for p in payloads if p.category in categories]
        if severities:
            payloads = [p for p in payloads if p.severity in severities]

        total = len(payloads)
        logger.info(f"Starting fuzz test with {total} payloads")

        report = FuzzReport(
            total_payloads=total,
            start_time=time.time(),
            target_url=self._url,
        )

        if total == 0:
            report.end_time = time.time()
            return report

        if not await self.connect():
            logger.error("Cannot establish initial connection for fuzzing")
            report.end_time = time.time()
            return report

        anomalies = 0
        for i, payload in enumerate(payloads):
            if (i + 1) % 10 == 0:
                logger.info(f"Fuzz progress: {i+1}/{total}")

            result = await self._send_and_observe(payload)
            self._results.append(result)

            if result.anomaly_detected:
                anomalies += 1
                atype = result.anomaly_type
                self._anomaly_counts[atype] = self._anomaly_counts.get(atype, 0) + 1
                logger.warning(
                    f"ANOMALY [{payload.id}] {payload.name}: {atype} "
                    f"({result.error_message[:80]})"
                )

            self._category_counts[payload.category] = (
                self._category_counts.get(payload.category, 0) + 1
            )
            self._severity_counts[payload.severity] = (
                self._severity_counts.get(payload.severity, 0) + 1
            )

            result.replay_steps = self._generate_replay_steps(payload, result)

            await asyncio.sleep(self._delay)

        report.anomalies_detected = anomalies
        report.clean_payloads = total - anomalies
        report.anomaly_counts = self._anomaly_counts
        report.category_counts = self._category_counts
        report.severity_counts = self._severity_counts
        report.results = self._results
        report.end_time = time.time()

        logger.info(f"Fuzz complete: {anomalies} anomalies / {total} payloads")
        await self._cleanup()
        return report

    @staticmethod
    def _generate_replay_steps(payload: FuzzPayload, result: FuzzResult) -> List[str]:
        steps = [
            f"1. Establish WebSocket connection to target server",
            f"2. Send join message for room '{payload.category or 'any'}'",
            f"3. Send fuzz payload [{payload.id}] '{payload.name}'",
        ]
        if isinstance(payload.raw_data, str) and payload.raw_data:
            snippet = payload.raw_data[:200]
            steps.append(f"4. Payload data (truncated): {snippet}")
        elif payload.generator:
            steps.append(f"4. Payload generated dynamically (see generator source)")
        if result.anomaly_detected:
            steps.append(f"5. Observe anomaly: {result.anomaly_type}")
            if result.error_message:
                steps.append(f"6. Error detail: {result.error_message[:200]}")
        return steps

    async def _cleanup(self):
        try:
            if self._ws is not None and not self._ws.closed:
                await self._ws.close()
        except Exception:
            pass
        try:
            if self._session is not None and not self._session.closed:
                await self._session.close()
        except Exception:
            pass


class BaselineComparer:
    @staticmethod
    def compare(baseline: dict, current: FuzzReport) -> dict:
        baseline_results = {
            r.get("payload_id"): r for r in baseline.get("results", [])
        }
        current_results = {r.payload_id: r for r in current.results}

        new_anomalies = []
        fixed_anomalies = []
        regressions = []

        for pid, curr in current_results.items():
            base = baseline_results.get(pid)
            if base:
                base_anomaly = base.get("anomaly_detected", False)
                if base_anomaly and not curr.anomaly_detected:
                    fixed_anomalies.append({
                        "payload_id": pid,
                        "name": curr.payload_name,
                        "old_anomaly": base.get("anomaly_type", ""),
                    })
                elif not base_anomaly and curr.anomaly_detected:
                    regressions.append({
                        "payload_id": pid,
                        "name": curr.payload_name,
                        "new_anomaly": curr.anomaly_type,
                    })
            else:
                if curr.anomaly_detected:
                    new_anomalies.append({
                        "payload_id": pid,
                        "name": curr.payload_name,
                        "anomaly_type": curr.anomaly_type,
                    })

        return {
            "baseline_total": baseline.get("total_payloads", 0),
            "current_total": current.total_payloads,
            "baseline_anomalies": baseline.get("anomalies_detected", 0),
            "current_anomalies": current.anomalies_detected,
            "new_anomalies": new_anomalies,
            "fixed_anomalies": fixed_anomalies,
            "regressions": regressions,
            "anomaly_delta": current.anomalies_detected - baseline.get("anomalies_detected", 0),
        }
