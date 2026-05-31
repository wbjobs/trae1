"""Web Dashboard 后端 API（aiohttp + WebSocket）

提供：
- REST API：/api/streams, /api/streams/{ssrc}, /api/report
- WebSocket：/ws 推送实时指标
- 静态文件：托管前端 Vue3 构建产物
"""

from __future__ import annotations

import asyncio
import json
import logging
import time
from pathlib import Path
from typing import Set

from aiohttp import web

from .ai_analyzer import ContentAnalysisResult
from .quality import StreamAnalyzer
from .report import export_report
from .window import StreamRegistry

logger = logging.getLogger(__name__)


class DashboardServer:
    """Web Dashboard 服务封装。"""

    PUSH_INTERVAL = 1.0  # 每秒推送一次

    def __init__(self, registry: StreamRegistry, host: str = "0.0.0.0",
                 port: int = 8080, static_dir: str = "web/static") -> None:
        self._registry = registry
        self._host = host
        self._port = port
        self._static_dir = Path(static_dir)
        self._static_dir.mkdir(parents=True, exist_ok=True)
        self._ws_clients: Set[web.WebSocketResponse] = set()
        self._app = web.Application()
        self._runner: Optional[web.AppRunner] = None
        self._push_task: Optional[asyncio.Task] = None
        self._setup_routes()

    def _setup_routes(self) -> None:
        self._app.router.add_get("/api/streams", self._handle_streams)
        self._app.router.add_get("/api/streams/{ssrc}", self._handle_stream_detail)
        self._app.router.add_post("/api/report", self._handle_report)
        self._app.router.add_get("/ws", self._handle_websocket)
        self._app.router.add_get("/", self._handle_index)
        self._app.router.add_static("/static", str(self._static_dir), name="static")

    async def _handle_index(self, request: web.Request) -> web.Response:
        index_file = self._static_dir / "index.html"
        if index_file.exists():
            return web.FileResponse(str(index_file))
        return web.Response(text="<h1>RTP Monitor Dashboard</h1><p>Frontend not built.</p>", content_type="text/html")

    async def _handle_streams(self, request: web.Request) -> web.Response:
        streams = self._registry.get_all_streams()
        ai_results = self._registry.get_all_ai_results()
        data = []
        for s in streams:
            latest = s.get_latest_metrics()
            ai = ai_results.get(s.info.ssrc)
            data.append({
                "ssrc": f"0x{s.info.ssrc:08X}",
                "ssrc_int": s.info.ssrc,
                "media_kind": s.info.media_kind,
                "codec": s.info.codec,
                "source_ip": s.info.source_ip,
                "source_port": s.info.source_port,
                "payload_type": s.info.payload_type,
                "total_packets": s.info.total_packets,
                "total_lost": s.info.total_lost,
                "last_seen": s.info.last_seen,
                "created_at": s.info.created_at,
                "latest": self._metrics_to_dict(latest) if latest else None,
                "ai": self._ai_result_to_dict(ai) if ai else None,
            })
        return web.json_response({"streams": data})

    async def _handle_stream_detail(self, request: web.Request) -> web.Response:
        try:
            ssrc = int(request.match_info["ssrc"], 0)
        except (ValueError, KeyError):
            return web.json_response({"error": "Invalid SSRC"}, status=400)
        analyzer = self._registry.get_stream(ssrc)
        if analyzer is None:
            return web.json_response({"error": "Stream not found"}, status=404)
        snaps = analyzer.get_window_metrics()
        ai_result = self._registry.get_latest_ai_result(ssrc)
        return web.json_response({
            "info": {
                "ssrc": f"0x{analyzer.info.ssrc:08X}",
                "media_kind": analyzer.info.media_kind,
                "codec": analyzer.info.codec,
                "source": f"{analyzer.info.source_ip}:{analyzer.info.source_port}",
            },
            "metrics": [self._metrics_to_dict(s) for s in snaps],
            "ai": self._ai_result_to_dict(ai_result) if ai_result else None,
        })

    async def _handle_report(self, request: web.Request) -> web.Response:
        try:
            filepath = export_report(self._registry)
            return web.json_response({"report_path": filepath})
        except Exception as exc:
            logger.exception("Report generation failed")
            return web.json_response({"error": str(exc)}, status=500)

    async def _handle_websocket(self, request: web.Request) -> web.WebSocketResponse:
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        self._ws_clients.add(ws)
        logger.info("WebSocket client connected, total: %d", len(self._ws_clients))
        try:
            async for msg in ws:
                if msg.type == web.WSMsgType.TEXT:
                    if msg.data == "ping":
                        await ws.send_json({"type": "pong", "timestamp": time.time()})
                elif msg.type == web.WSMsgType.ERROR:
                    break
        finally:
            self._ws_clients.discard(ws)
            logger.info("WebSocket client disconnected, total: %d", len(self._ws_clients))
        return ws

    async def _push_loop(self) -> None:
        while True:
            await asyncio.sleep(self.PUSH_INTERVAL)
            if not self._ws_clients:
                continue
            data = self._build_push_payload()
            dead = []
            for ws in list(self._ws_clients):
                try:
                    await ws.send_json(data)
                except Exception:
                    dead.append(ws)
            for ws in dead:
                self._ws_clients.discard(ws)

    def _build_push_payload(self) -> dict:
        streams = self._registry.get_all_streams()
        ai_results = self._registry.get_all_ai_results()
        return {
            "type": "metrics",
            "timestamp": time.time(),
            "streams": [
                {
                    "ssrc": f"0x{s.info.ssrc:08X}",
                    "ssrc_int": s.info.ssrc,
                    "media_kind": s.info.media_kind,
                    "codec": s.info.codec,
                    "latest": self._metrics_to_dict(s.get_latest_metrics()),
                    "ai": self._ai_result_to_dict(ai_results.get(s.info.ssrc)),
                }
                for s in streams
            ],
        }

    @staticmethod
    def _ai_result_to_dict(ai: Optional[ContentAnalysisResult]) -> dict:
        if ai is None:
            return None
        return {
            "timestamp": ai.timestamp,
            "anomaly_score": ai.anomaly_score,
            "is_anomaly": ai.is_anomaly,
            "threshold": ai.threshold,
            "audio_anomalies": ai.audio_anomalies,
            "video_anomalies": ai.video_anomalies,
            "root_cause": ai.root_cause,
            "root_cause_confidence": ai.root_cause_confidence,
            "rtp_loss_rate": ai.rtp_loss_rate,
            "rtp_jitter": ai.rtp_jitter,
            "rtp_mos": ai.rtp_mos,
            "audio_scores": ai.audio_scores,
            "video_scores": ai.video_scores,
        }

    @staticmethod
    def _metrics_to_dict(snap) -> dict:
        if snap is None:
            return None
        return {
            "timestamp": snap.timestamp,
            "packet_count": snap.packet_count,
            "byte_count": snap.byte_count,
            "lost_packets": snap.lost_packets,
            "loss_rate": snap.loss_rate,
            "jitter": snap.jitter,
            "delay": snap.delay,
            "mos": snap.mos,
            "fps": snap.fps,
            "bitrate": snap.bitrate,
            "width": snap.width,
            "height": snap.height,
            "codec": snap.codec,
        }

    async def start(self) -> None:
        self._runner = web.AppRunner(self._app)
        await self._runner.setup()
        site = web.TCPSite(self._runner, self._host, self._port)
        await site.start()
        self._push_task = asyncio.create_task(self._push_loop())
        logger.info("Dashboard server started on http://%s:%d", self._host, self._port)

    async def stop(self) -> None:
        if self._push_task:
            self._push_task.cancel()
            try:
                await self._push_task
            except asyncio.CancelledError:
                pass
        if self._runner:
            await self._runner.cleanup()
        logger.info("Dashboard server stopped")
