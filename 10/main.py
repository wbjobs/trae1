import logging
import asyncio
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from config import settings
from routers.metrics import router as metrics_router
from routers.alerts import router as alerts_router
from routers.config import router as config_router
from services.metrics_service import get_metrics_service
from db.influxdb_client import get_influxdb_manager

logging.basicConfig(
    level=logging.DEBUG if settings.DEBUG else logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

collector_task = None


async def periodic_collector():
    service = get_metrics_service()
    while True:
        try:
            result = await asyncio.to_thread(service.collect_and_report)
            if not result.get("success"):
                logger.warning(f"周期采集失败: {result.get('message')}")
        except asyncio.TimeoutError:
            logger.error("周期采集超时")
        except Exception as e:
            logger.error(f"周期采集异常: {e}")
        await asyncio.sleep(settings.COLLECT_INTERVAL)


@asynccontextmanager
async def lifespan(app: FastAPI):
    global collector_task
    logger.info(f"启动 {settings.APP_NAME} v{settings.APP_VERSION}")
    logger.info(f"InfluxDB连接: {settings.INFLUXDB_URL}")

    try:
        db = get_influxdb_manager()
        logger.info("InfluxDB客户端初始化成功")
    except Exception as e:
        logger.error(f"InfluxDB客户端初始化失败: {e}")

    collector_task = asyncio.create_task(periodic_collector())
    logger.info(f"启动周期采集任务, 间隔: {settings.COLLECT_INTERVAL}秒")

    yield

    if collector_task:
        collector_task.cancel()
        try:
            await collector_task
        except asyncio.CancelledError:
            pass
        logger.info("周期采集任务已停止")

    db = get_influxdb_manager()
    db.close()
    logger.info("应用已关闭")


app = FastAPI(
    title=settings.APP_NAME,
    version=settings.APP_VERSION,
    description="服务器资源指标采集API服务 - 支持CPU/内存/磁盘/网络指标采集、历史数据存储、资源过载告警",
    lifespan=lifespan,
    docs_url="/docs",
    redoc_url="/redoc"
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(metrics_router)
app.include_router(alerts_router)
app.include_router(config_router)


@app.get("/", summary="服务状态检查")
async def root():
    return {
        "name": settings.APP_NAME,
        "version": settings.APP_VERSION,
        "status": "running",
        "collect_interval": f"{settings.COLLECT_INTERVAL}s",
        "thresholds": {
            "cpu_usage": f"{settings.CPU_USAGE_THRESHOLD}%",
            "memory_usage": f"{settings.MEMORY_USAGE_THRESHOLD}%",
            "disk_usage": f"{settings.DISK_USAGE_THRESHOLD}%",
            "network_throughput": f"{settings.NETWORK_THROUGHPUT_THRESHOLD}MB/s"
        },
        "endpoints": {
            "collect": "/api/metrics/collect",
            "batch_collect": "/api/metrics/collect/batch",
            "query": "/api/metrics/query",
            "trend": "/api/metrics/trend",
            "ranking": "/api/metrics/ranking",
            "overloaded": "/api/metrics/overloaded",
            "alerts": "/api/alerts",
            "thresholds": "/api/config/thresholds",
            "denoise_config": "/api/metrics/denoise/config",
            "docs": "/docs",
            "redoc": "/redoc"
        }
    }


@app.get("/health", summary="健康检查")
async def health_check():
    try:
        db = get_influxdb_manager()
        return {
            "status": "healthy",
            "influxdb": "connected",
            "timestamp": __import__("datetime").datetime.utcnow().isoformat()
        }
    except Exception as e:
        return {
            "status": "unhealthy",
            "influxdb": "disconnected",
            "error": str(e)
        }


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        "main:app",
        host=settings.HOST,
        port=settings.PORT,
        reload=settings.DEBUG
    )
