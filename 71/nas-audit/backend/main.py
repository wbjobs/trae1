import uvicorn

from src.core.config import load_config
from src.core.logger import setup_logging


def main():
    config = load_config()
    setup_logging(config.logging.level, config.logging.file)

    uvicorn.run(
        "src.api.service:app",
        host=config.api.host,
        port=config.api.port,
        reload=False,
        log_level=config.logging.level.lower(),
    )


if __name__ == "__main__":
    main()
