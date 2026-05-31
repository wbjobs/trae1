import logging
from dataclasses import dataclass, field
from typing import List, Optional, Dict

import yaml

logger = logging.getLogger(__name__)


@dataclass
class SensitiveWord:
    word: str
    category: str = "default"
    regex: bool = False
    severity: str = "high"
    description: str = ""


@dataclass
class SensitiveConfig:
    enabled: bool = True
    tika_server: str = "http://localhost:9998"
    tika_timeout: int = 30
    scan_extensions: List[str] = field(default_factory=lambda: [
        ".doc", ".docx", ".pdf", ".txt", ".xls", ".xlsx",
        ".ppt", ".pptx", ".csv", ".rtf",
    ])
    max_file_size_mb: int = 50
    min_text_length: int = 2
    max_text_length: int = 500000
    similarity_threshold: float = 0.85
    snippet_context_chars: int = 50
    scan_concurrency: int = 4
    alert_cooldown_seconds: int = 300
    words: List[SensitiveWord] = field(default_factory=list)


@dataclass
class AlertConfig:
    enabled: bool = True
    wecom_webhook: str = ""
    wecom_mentioned_mobiles: List[str] = field(default_factory=list)
    smtp_host: str = ""
    smtp_port: int = 465
    smtp_use_ssl: bool = True
    smtp_username: str = ""
    smtp_password: str = ""
    smtp_from: str = ""
    smtp_to: List[str] = field(default_factory=list)
    alert_on_severity: List[str] = field(default_factory=lambda: ["high", "critical"])


@dataclass
class QuarantineConfig:
    enabled: bool = True
    quarantine_path: str = "Quarantine"
    auto_quarantine: bool = False
    auto_quarantine_severity: List[str] = field(default_factory=lambda: ["critical"])


@dataclass
class DLPConfig:
    sensitive: SensitiveConfig
    alert: AlertConfig
    quarantine: QuarantineConfig


def _parse_sensitive_words(raw_list: List[dict]) -> List[SensitiveWord]:
    words = []
    for item in raw_list:
        w = SensitiveWord(
            word=item.get("word", ""),
            category=item.get("category", "default"),
            regex=item.get("regex", False),
            severity=item.get("severity", "high"),
            description=item.get("description", ""),
        )
        if w.word:
            words.append(w)
    return words


def load_dlp_config(config_path: str = "config.yaml") -> DLPConfig:
    with open(config_path, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f)

    dlp_raw = raw.get("dlp", {})
    sens_raw = dlp_raw.get("sensitive", {})
    alert_raw = dlp_raw.get("alert", {})
    quar_raw = dlp_raw.get("quarantine", {})

    sens_cfg = SensitiveConfig(
        enabled=sens_raw.get("enabled", True),
        tika_server=sens_raw.get("tika_server", "http://localhost:9998"),
        tika_timeout=sens_raw.get("tika_timeout", 30),
        scan_extensions=sens_raw.get("scan_extensions", [
            ".doc", ".docx", ".pdf", ".txt", ".xls", ".xlsx",
            ".ppt", ".pptx", ".csv", ".rtf",
        ]),
        max_file_size_mb=sens_raw.get("max_file_size_mb", 50),
        min_text_length=sens_raw.get("min_text_length", 2),
        max_text_length=sens_raw.get("max_text_length", 500000),
        similarity_threshold=sens_raw.get("similarity_threshold", 0.85),
        snippet_context_chars=sens_raw.get("snippet_context_chars", 50),
        scan_concurrency=sens_raw.get("scan_concurrency", 4),
        alert_cooldown_seconds=sens_raw.get("alert_cooldown_seconds", 300),
        words=_parse_sensitive_words(sens_raw.get("words", [])),
    )

    alert_cfg = AlertConfig(
        enabled=alert_raw.get("enabled", True),
        wecom_webhook=alert_raw.get("wecom_webhook", ""),
        wecom_mentioned_mobiles=alert_raw.get("wecom_mentioned_mobiles", []),
        smtp_host=alert_raw.get("smtp_host", ""),
        smtp_port=alert_raw.get("smtp_port", 465),
        smtp_use_ssl=alert_raw.get("smtp_use_ssl", True),
        smtp_username=alert_raw.get("smtp_username", ""),
        smtp_password=alert_raw.get("smtp_password", ""),
        smtp_from=alert_raw.get("smtp_from", ""),
        smtp_to=alert_raw.get("smtp_to", []),
        alert_on_severity=alert_raw.get("alert_on_severity", ["high", "critical"]),
    )

    quar_cfg = QuarantineConfig(
        enabled=quar_raw.get("enabled", True),
        quarantine_path=quar_raw.get("quarantine_path", "Quarantine"),
        auto_quarantine=quar_raw.get("auto_quarantine", False),
        auto_quarantine_severity=quar_raw.get("auto_quarantine_severity", ["critical"]),
    )

    return DLPConfig(sensitive=sens_cfg, alert=alert_cfg, quarantine=quar_cfg)
