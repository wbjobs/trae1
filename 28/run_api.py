"""Launch the FastAPI server (uvicorn)."""
import uvicorn

from ebpf_monitor.config import settings

if __name__ == "__main__":  # pragma: no cover
    uvicorn.run(
        "ebpf_monitor.app:app",
        host=settings.api_host,
        port=settings.api_port,
        reload=False,
    )
