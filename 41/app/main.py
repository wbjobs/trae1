import logging
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from app.api.routes import router, get_scan_engine
from app.db.models import init_db

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
)
logger = logging.getLogger(__name__)

app = FastAPI(
    title="Malware Scanner Service",
    description="YARA-based malware file scanning service with hot reload support",
    version="1.1.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(router)


@app.on_event("startup")
async def startup_event():
    init_db()
    get_scan_engine()
    logger.info("Malware Scanner Service started with hot reload support")


@app.on_event("shutdown")
async def shutdown_event():
    scan_engine = app.state.scan_engine if hasattr(app.state, 'scan_engine') else None
    if scan_engine:
        scan_engine.shutdown()
    logger.info("Malware Scanner Service shutdown complete")


@app.get("/")
async def root():
    return {
        "name": "Malware Scanner Service",
        "version": "1.1.0",
        "docs": "/docs",
        "features": [
            "YARA rule-based scanning",
            "Hot rule reload",
            "MD5 deduplication",
            "Batch scanning",
            "PostgreSQL persistence",
        ],
    }
