"""全局日志配置。"""
from __future__ import annotations

import logging
import sys
from typing import Optional


_LOG_FORMAT = "%(asctime)s [%(levelname)s] %(name)s: %(message)s"
_DATE_FORMAT = "%Y-%m-%d %H:%M:%S"


def get_logger(name: str, level: int = logging.INFO) -> logging.Logger:
    logger = logging.getLogger(name)
    if not logger.handlers:
        handler = logging.StreamHandler(sys.stderr)
        handler.setFormatter(logging.Formatter(_LOG_FORMAT, _DATE_FORMAT))
        logger.addHandler(handler)
    logger.setLevel(level)
    return logger


def set_global_level(level: int) -> None:
    logging.getLogger("mylvmbackup").setLevel(level)


def parse_log_level(value: Optional[str]) -> int:
    if not value:
        return logging.INFO
    return getattr(logging, value.upper(), logging.INFO)
