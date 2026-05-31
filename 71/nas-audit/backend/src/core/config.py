import os
import logging
from dataclasses import dataclass, field
from typing import List, Optional
from pathlib import Path

import yaml
from pydantic_settings import BaseSettings
from dotenv import load_dotenv

load_dotenv()

logger = logging.getLogger(__name__)


class Settings(BaseSettings):
    smb_server: str = os.getenv("SMB_SERVER", "192.168.1.100")
    smb_port: int = int(os.getenv("SMB_PORT", "445"))
    smb_username: str = os.getenv("SMB_USERNAME", "admin")
    smb_password: str = os.getenv("SMB_PASSWORD", "")
    smb_domain: str = os.getenv("SMB_DOMAIN", "CORP")
    smb_share: str = os.getenv("SMB_SHARE", "SharedFiles")

    elasticsearch_hosts: List[str] = os.getenv("ELASTICSEARCH_HOSTS", "http://localhost:9200").split(",")
    elasticsearch_user: str = os.getenv("ELASTICSEARCH_USER", "elastic")
    elasticsearch_password: str = os.getenv("ELASTICSEARCH_PASSWORD", "")

    api_host: str = os.getenv("API_HOST", "0.0.0.0")
    api_port: int = int(os.getenv("API_PORT", "8000"))

    class Config:
        env_file = ".env"


@dataclass
class SMBConfig:
    server: str
    port: int
    username: str
    password: str
    domain: str
    share_name: str
    watch_paths: List[str]
    poll_interval: int
    monitored_extensions: List[str]
    dedup_window_ms: int = 200
    state_file: str = "data/audit_state.json"


@dataclass
class ElasticsearchConfig:
    hosts: List[str]
    username: str
    password: str
    index_prefix: str
    retention_days: int


@dataclass
class APIConfig:
    host: str
    port: int
    cors_origins: List[str]


@dataclass
class LoggingConfig:
    level: str
    file: str


@dataclass
class AppConfig:
    smb: SMBConfig
    elasticsearch: ElasticsearchConfig
    api: APIConfig
    logging: LoggingConfig


def load_config(config_path: str = "config.yaml") -> AppConfig:
    with open(config_path, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f)

    smb_raw = raw["smb"]
    smb_cfg = SMBConfig(
        server=smb_raw["server"],
        port=smb_raw["port"],
        username=smb_raw["username"],
        password=smb_raw["password"],
        domain=smb_raw["domain"],
        share_name=smb_raw["share_name"],
        watch_paths=smb_raw["watch_paths"],
        poll_interval=smb_raw["poll_interval"],
        monitored_extensions=smb_raw["monitored_extensions"],
        dedup_window_ms=smb_raw.get("dedup_window_ms", 200),
        state_file=smb_raw.get("state_file", "data/audit_state.json"),
    )

    es_cfg = ElasticsearchConfig(
        hosts=raw["elasticsearch"]["hosts"],
        username=raw["elasticsearch"]["username"],
        password=raw["elasticsearch"]["password"],
        index_prefix=raw["elasticsearch"]["index_prefix"],
        retention_days=raw["elasticsearch"]["retention_days"],
    )

    api_cfg = APIConfig(
        host=raw["api"]["host"],
        port=raw["api"]["port"],
        cors_origins=raw["api"]["cors_origins"],
    )

    log_cfg = LoggingConfig(
        level=raw["logging"]["level"],
        file=raw["logging"]["file"],
    )

    return AppConfig(smb=smb_cfg, elasticsearch=es_cfg, api=api_cfg, logging=log_cfg)
