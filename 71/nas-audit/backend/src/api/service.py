import logging
import time
import asyncio
import queue
import threading
from typing import Optional, List

from fastapi import FastAPI, HTTPException, Query, Body
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse

from ..core.config import AppConfig, load_config
from ..core.es_store import ElasticsearchStore
from ..core.smb_monitor import SMBFileMonitor
from ..core.state_manager import StateManager
from ..core.dlp_config import load_dlp_config, DLPConfig
from ..core.dlp_pipeline import DLPPipeline
from ..core.event_models import FileOperationEvent
from . import schemas

logger = logging.getLogger(__name__)

BATCH_SIZE = 200
BATCH_FLUSH_INTERVAL = 1.0


class NASAuditAPI:
    def __init__(self, config: AppConfig):
        self.config = config
        self.dlp_cfg = load_dlp_config()

        self.app = FastAPI(
            title="NAS Audit Service API",
            description="SMB/NAS文件操作审计服务 + 敏感文件泄露检测",
            version="2.0.0",
        )
        self.es_store = ElasticsearchStore(config.elasticsearch)
        self.state_manager = StateManager(
            state_file=config.smb.state_file,
            max_processed_events=10000,
        )
        self.smb_monitor: Optional[SMBFileMonitor] = None
        self.dlp_pipeline: Optional[DLPPipeline] = None
        self._start_time = time.time()
        self._smb_connected = False

        self._event_queue: "queue.Queue[FileOperationEvent]" = queue.Queue()
        self._batch_buffer: List[FileOperationEvent] = []
        self._batch_lock = threading.Lock()
        self._consumer_thread: Optional[threading.Thread] = None
        self._stop_consumer = threading.Event()

        self._setup_cors()
        self._setup_routes()

    def _setup_cors(self):
        self.app.add_middleware(
            CORSMiddleware,
            allow_origins=self.config.api.cors_origins,
            allow_credentials=True,
            allow_methods=["*"],
            allow_headers=["*"],
        )

    def _setup_routes(self):
        app = self.app

        @app.get("/api/health", tags=["System"])
        async def health_check():
            es_ok = False
            if self.es_store._client:
                try:
                    await self.es_store._client.ping()
                    es_ok = True
                except Exception:
                    pass

            dlp_enabled = self.dlp_cfg.sensitive.enabled
            tika_ok = False
            if self.dlp_pipeline and self.dlp_pipeline.extractor:
                tika_ok = self.dlp_pipeline.extractor.is_available()

            return {
                "status": "ok" if es_ok else "degraded",
                "elasticsearch": es_ok,
                "smb_connected": self._smb_connected,
                "dlp_enabled": dlp_enabled,
                "tika_available": tika_ok,
                "uptime_seconds": time.time() - self._start_time,
            }

        @app.get("/api/events", tags=["Events"])
        async def query_events(
            username: Optional[str] = Query(None, description="用户名过滤"),
            start_time: Optional[float] = Query(None, description="开始时间戳（Unix时间）"),
            end_time: Optional[float] = Query(None, description="结束时间戳（Unix时间）"),
            extensions: Optional[str] = Query(None, description="文件扩展名（逗号分隔）"),
            operation_type: Optional[str] = Query(None, description="操作类型：create/delete/rename/modify"),
            file_path: Optional[str] = Query(None, description="文件路径关键字"),
            source_ip: Optional[str] = Query(None, description="源IP地址"),
            size: int = Query(100, ge=1, le=10000, description="每页数量"),
            scroll_id: Optional[str] = Query(None, description="滚动ID"),
        ):
            ext_list = None
            if extensions:
                ext_list = [ext.strip().lower() for ext in extensions.split(",") if ext.strip()]

            result = await self.es_store.query_events(
                username=username,
                start_time=start_time,
                end_time=end_time,
                extensions=ext_list,
                operation_type=operation_type,
                file_path=file_path,
                source_ip=source_ip,
                size=size,
                scroll_id=scroll_id,
            )

            events = []
            for evt in result.get("events", []):
                events.append(schemas.EventResponse(
                    id=evt.get("_id", ""),
                    operation_type=evt.get("operation_type", ""),
                    file_path=evt.get("file_path", ""),
                    old_file_path=evt.get("old_file_path"),
                    timestamp=evt.get("timestamp", 0),
                    username=evt.get("username", ""),
                    source_ip=evt.get("source_ip", ""),
                    file_size=evt.get("file_size", 0),
                    file_extension=evt.get("file_extension", ""),
                    iso_timestamp=evt.get("@timestamp", ""),
                ))

            return {
                "total": result.get("total", 0),
                "events": events,
                "scroll_id": result.get("scroll_id"),
            }

        @app.get("/api/dashboard", tags=["Dashboard"])
        async def get_dashboard(
            days: int = Query(7, ge=1, le=90),
            top_n: int = Query(10, ge=1, le=50),
        ):
            top_users_raw = await self.es_store.get_top_users(top_n=top_n, days=days)
            trend_raw = await self.es_store.get_operation_trend(days=days)
            ext_stats_raw = await self.es_store.get_extension_stats(days=days)

            return {
                "top_users": top_users_raw,
                "operation_trend": trend_raw,
                "extension_stats": ext_stats_raw,
            }

        @app.post("/api/events/manual", tags=["Events"])
        async def ingest_manual_event(event_data: dict):
            try:
                from ..core.event_models import OperationType as OT
                evt = FileOperationEvent(
                    operation_type=OT(event_data.get("operation_type", "access")),
                    file_path=event_data.get("file_path", ""),
                    timestamp=event_data.get("timestamp", time.time()),
                    username=event_data.get("username", ""),
                    source_ip=event_data.get("source_ip", ""),
                    file_size=event_data.get("file_size", 0),
                    old_file_path=event_data.get("old_file_path"),
                    file_extension=event_data.get("file_extension", ""),
                )
                result = await self.es_store.index_event(evt)
                return {"status": "success", "id": result}
            except Exception as e:
                raise HTTPException(status_code=400, detail=str(e))

        @app.post("/api/system/cleanup", tags=["System"])
        async def trigger_cleanup():
            await self.es_store.cleanup_old_indices()
            return {"status": "success", "message": "Cleanup triggered"}

        @app.get("/api/dlp/status", tags=["DLP"])
        async def dlp_status():
            return {
                "enabled": self.dlp_cfg.sensitive.enabled,
                "tika_server": self.dlp_cfg.sensitive.tika_server,
                "tika_available": self.dlp_pipeline.extractor.is_available() if self.dlp_pipeline else False,
                "scan_extensions": self.dlp_cfg.sensitive.scan_extensions,
                "max_file_size_mb": self.dlp_cfg.sensitive.max_file_size_mb,
                "alert_enabled": self.dlp_cfg.alert.enabled,
                "wecom_configured": bool(self.dlp_cfg.alert.wecom_webhook),
                "email_configured": bool(self.dlp_cfg.alert.smtp_host and self.dlp_cfg.alert.smtp_to),
                "quarantine_enabled": self.dlp_cfg.quarantine.enabled,
                "quarantine_path": self.dlp_cfg.quarantine.quarantine_path,
                "auto_quarantine": self.dlp_cfg.quarantine.auto_quarantine,
                "word_count": len(self.dlp_cfg.sensitive.words),
            }

        @app.get("/api/dlp/words", tags=["DLP"])
        async def list_sensitive_words():
            if not self.dlp_pipeline:
                return {"words": []}
            return {"words": self.dlp_pipeline.get_current_words()}

        @app.post("/api/dlp/words/reload", tags=["DLP"])
        async def reload_sensitive_words(words: list = Body(...)):
            if not self.dlp_pipeline:
                raise HTTPException(status_code=400, detail="DLP pipeline not running")
            self.dlp_pipeline.reload_words(words)
            return {"status": "success", "count": len(words)}

        @app.post("/api/dlp/scan-text", tags=["DLP"])
        async def scan_text_for_sensitive_content(request: dict = Body(...)):
            text = request.get("text", "")
            if not self.dlp_pipeline:
                raise HTTPException(status_code=400, detail="DLP pipeline not running")
            if not text:
                raise HTTPException(status_code=400, detail="text is required")
            matches = self.dlp_pipeline.scan_content(text)
            return {
                "text_length": len(text),
                "match_count": len(matches),
                "matches": matches,
            }

        @app.get("/api/dlp/events", tags=["DLP"])
        async def query_dlp_events(
            severity: Optional[str] = Query(None, description="严重等级: critical/high/medium/low"),
            category: Optional[str] = Query(None, description="分类"),
            username: Optional[str] = Query(None),
            file_path: Optional[str] = Query(None),
            start_time: Optional[float] = Query(None),
            end_time: Optional[float] = Query(None),
            quarantined: Optional[bool] = Query(None),
            size: int = Query(100, ge=1, le=10000),
            scroll_id: Optional[str] = Query(None),
        ):
            must = []

            if start_time or end_time:
                range_q = {}
                if start_time:
                    range_q["gte"] = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(start_time))
                if end_time:
                    range_q["lte"] = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(end_time))
                must.append({"range": {"@timestamp": range_q}})

            if severity:
                must.append({"term": {"top_severity": severity}})
            if category:
                must.append({"term": {"categories": category}})
            if username:
                must.append({"term": {"event.username": username}})
            if file_path:
                must.append({"wildcard": {"event.file_path": f"*{file_path}*"}})
            if quarantined is not None:
                must.append({"term": {"quarantined": quarantined}})

            query = {"bool": {"must": must}} if must else {"match_all": {}}

            index = f"{self.es_store.config.index_prefix}_dlp-*"
            try:
                if scroll_id:
                    result = await self.es_store._client.scroll(scroll_id=scroll_id, scroll="2m")
                else:
                    result = await self.es_store._client.search(
                        index=index,
                        query=query,
                        size=size,
                        sort=[{"@timestamp": {"order": "desc"}}],
                        scroll="2m",
                    )

                events = []
                for hit in result.get("hits", {}).get("hits", []):
                    src = hit["_source"]
                    src["_id"] = hit["_id"]
                    events.append(src)

                total = result.get("hits", {}).get("total", {}).get("value", 0)
                return {
                    "total": total,
                    "events": events,
                    "scroll_id": result.get("_scroll_id"),
                }
            except Exception as e:
                logger.error(f"DLP events query error: {e}")
                raise HTTPException(status_code=500, detail=str(e))

        @app.post("/api/dlp/quarantine", tags=["DLP"])
        async def quarantine_file(request: dict = Body(...)):
            file_path = request.get("file_path", "")
            if not file_path:
                raise HTTPException(status_code=400, detail="file_path is required")
            if not self.dlp_pipeline:
                raise HTTPException(status_code=400, detail="DLP pipeline not running")

            success, quarantine_path, isolation_id = self.dlp_pipeline.quarantine_mgr.quarantine_file(
                self.smb_monitor,
                file_path,
                username=request.get("username", "manual"),
                reason=request.get("reason", "Manual quarantine via API"),
            )
            if not success:
                raise HTTPException(status_code=500, detail="Quarantine failed")

            return {
                "status": "success",
                "original_path": file_path,
                "quarantine_path": quarantine_path,
                "isolation_id": isolation_id,
            }

        @app.post("/api/dlp/restore", tags=["DLP"])
        async def restore_file(request: dict = Body(...)):
            quarantine_path = request.get("quarantine_path", "")
            original_path = request.get("original_path", "")
            if not quarantine_path or not original_path:
                raise HTTPException(status_code=400, detail="quarantine_path and original_path are required")
            if not self.dlp_pipeline:
                raise HTTPException(status_code=400, detail="DLP pipeline not running")

            success = self.dlp_pipeline.quarantine_mgr.restore_file(
                self.smb_monitor,
                quarantine_path,
                original_path,
            )
            if not success:
                raise HTTPException(status_code=500, detail="Restore failed")

            return {"status": "success", "restored_to": original_path}

        @app.get("/api/dlp/quarantine/list", tags=["DLP"])
        async def list_quarantined_files():
            if not self.dlp_pipeline:
                return {"files": []}
            files = self.dlp_pipeline.quarantine_mgr.list_quarantined(self.smb_monitor)
            return {"files": files}

        @app.post("/api/dlp/alert/test", tags=["DLP"])
        async def send_test_alert():
            if not self.dlp_pipeline:
                raise HTTPException(status_code=400, detail="DLP pipeline not running")
            result = self.dlp_pipeline.alert_mgr.send_test_alert()
            return {"status": "sent", "channels": result}

    def _on_event_sync(self, event: FileOperationEvent):
        self._event_queue.put(event)
        if self.dlp_pipeline and self.dlp_cfg.sensitive.enabled:
            self.dlp_pipeline.submit(event)

    def _consumer_loop(self):
        loop = asyncio.new_event_loop()

        async def flush_batch(batch):
            if not batch:
                return
            try:
                success = await self.es_store.index_events_bulk(batch)
                logger.info(f"Batch indexed: {success}/{len(batch)} events")
            except Exception as e:
                logger.error(f"Batch index error: {e}")

        async def _consumer_async():
            while not self._stop_consumer.is_set():
                try:
                    event = self._event_queue.get(timeout=0.5)
                    with self._batch_lock:
                        self._batch_buffer.append(event)

                    if len(self._batch_buffer) >= BATCH_SIZE:
                        with self._batch_lock:
                            batch = self._batch_buffer
                            self._batch_buffer = []
                        await flush_batch(batch)
                except queue.Empty:
                    pass

                if self._batch_buffer:
                    with self._batch_lock:
                        batch = self._batch_buffer
                        self._batch_buffer = []
                    await flush_batch(batch)

        try:
            loop.run_until_complete(_consumer_async())
        finally:
            loop.close()

    async def start(self):
        logger.info("Starting NAS Audit API service...")
        await self.es_store.connect()

        self.smb_monitor = SMBFileMonitor(self.config.smb, self.state_manager)
        self.smb_monitor.register_callback(self._on_event_sync)

        if self.dlp_cfg.sensitive.enabled:
            self.dlp_pipeline = DLPPipeline(self.dlp_cfg, self.es_store, self.smb_monitor)
            self.dlp_pipeline.start()
            logger.info("DLP pipeline started")

        self._consumer_thread = threading.Thread(
            target=self._consumer_loop, daemon=True, name="event-consumer"
        )
        self._consumer_thread.start()

        self.smb_monitor.start()
        self._smb_connected = self.smb_monitor._connected

        await self.es_store.cleanup_old_indices()
        logger.info("NAS Audit API service started")

    async def stop(self):
        logger.info("Stopping NAS Audit API service...")
        self._stop_consumer.set()

        if self.dlp_pipeline:
            self.dlp_pipeline.stop()

        if self.smb_monitor:
            self.smb_monitor.stop()

        if self._consumer_thread:
            self._consumer_thread.join(timeout=5)
            self._consumer_thread = None

        self._smb_connected = False
        await self.es_store.close()
        logger.info("NAS Audit API service stopped")


def create_app() -> FastAPI:
    config = load_config()
    api_service = NASAuditAPI(config)

    @api_service.app.on_event("startup")
    async def startup():
        await api_service.start()

    @api_service.app.on_event("shutdown")
    async def shutdown():
        await api_service.stop()

    return api_service.app


app = create_app()
