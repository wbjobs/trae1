"""Configuration loader module"""
import os
import yaml
from typing import Any, Dict, Optional, List
from dataclasses import dataclass, field


@dataclass
class GatewayConfig:
    name: str = "rabbitmq_audit_gateway"
    host: str = "0.0.0.0"
    port: int = 5672
    management_port: int = 15672
    vhost: str = "/"
    username: str = "guest"
    password: str = "guest"
    heartbeat: int = 60
    connection_timeout: int = 30


@dataclass
class HAConfig:
    mode: str = "active_standby"
    health_check_interval: int = 5
    election_timeout: int = 10
    shared_lock_path: str = "/tmp/gateway_lock"


@dataclass
class AuditConfig:
    enabled: bool = True
    log_level: str = "INFO"
    sample_rate: float = 0.01
    sample_by_type: bool = False
    batch_size: int = 100
    flush_interval: int = 5
    max_body_size: int = 1048576  # 1MB
    preview_size: int = 1024  # 1KB


@dataclass
class SamplingConfig:
    default_rate: float = 0.01
    large_message_threshold: int = 1048576  # 1MB
    large_message_rate: float = 0.001
    by_message_type: Dict[str, float] = field(default_factory=lambda: {
        "critical": 1.0,
        "normal": 0.1,
        "low": 0.01
    })


@dataclass
class InterceptionConfig:
    enabled: bool = True
    rules_file: str = "config/interception_rules.yaml"
    block_action: str = "drop"
    log_blocked: bool = True
    check_prefix_size: int = 4096  # 4KB
    check_suffix_size: int = 4096  # 4KB


@dataclass
class KafkaSinkConfig:
    enabled: bool = False
    bootstrap_servers: str = "localhost:9092"
    topic: str = "rabbitmq_audit_logs"
    compression: str = "gzip"


@dataclass
class ElasticsearchSinkConfig:
    enabled: bool = True
    hosts: list = field(default_factory=lambda: ["http://localhost:9200"])
    index: str = "rabbitmq_audit"
    username: str = "elastic"
    password: str = "changeme"


@dataclass
class SinksConfig:
    kafka: KafkaSinkConfig = field(default_factory=KafkaSinkConfig)
    elasticsearch: ElasticsearchSinkConfig = field(default_factory=ElasticsearchSinkConfig)


@dataclass
class DashboardConfig:
    host: str = "0.0.0.0"
    port: int = 8080
    refresh_interval: int = 5


@dataclass
class OtelConfig:
    enabled: bool = True
    service_name: str = "rabbitmq_audit_gateway"
    exporter: str = "otlp"
    endpoint: str = "http://localhost:4317"
    insecure: bool = True


@dataclass
class ClamAVConfig:
    enabled: bool = False
    host: str = "localhost"
    port: int = 3310
    socket_path: str = "/var/run/clamav/clamd.sock"


@dataclass
class YARAConfig:
    enabled: bool = False
    rules_dir: str = "yara_rules"


@dataclass
class VirusScanConfig:
    enabled: bool = False
    scan_mode: str = "async"
    scan_timeout: int = 5
    quarantine_exchange: str = "audit.quarantine"
    auto_quarantine: bool = True
    notify_on_infection: bool = True
    max_threads: int = 4
    max_body_size: int = 10485760
    min_attachment_size: int = 1024
    skip_large_messages: bool = True
    db_update_interval_hours: int = 24
    clamav: ClamAVConfig = field(default_factory=ClamAVConfig)
    yara: YARAConfig = field(default_factory=YARAConfig)


@dataclass
class AppConfig:
    gateway: GatewayConfig = field(default_factory=GatewayConfig)
    ha: HAConfig = field(default_factory=HAConfig)
    audit: AuditConfig = field(default_factory=AuditConfig)
    sampling: SamplingConfig = field(default_factory=SamplingConfig)
    interception: InterceptionConfig = field(default_factory=InterceptionConfig)
    sinks: SinksConfig = field(default_factory=SinksConfig)
    dashboard: DashboardConfig = field(default_factory=DashboardConfig)
    otel: OtelConfig = field(default_factory=OtelConfig)
    virus_scan: VirusScanConfig = field(default_factory=VirusScanConfig)


def _dict_to_obj(d: Dict[str, Any], cls: Any) -> Any:
    if isinstance(d, dict):
        field_values = {}
        for key, value in d.items():
            if key in cls.__dataclass_fields__:
                field_type = cls.__dataclass_fields__[key].type
                if isinstance(value, dict) and hasattr(field_type, '__dataclass_fields__'):
                    field_values[key] = _dict_to_obj(value, field_type)
                else:
                    field_values[key] = value
        return cls(**field_values)
    return d


def load_config(config_path: Optional[str] = None) -> AppConfig:
    if config_path is None:
        config_path = os.environ.get("CONFIG_PATH", "config/config.yaml")

    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    full_path = os.path.join(base_dir, config_path)

    if not os.path.exists(full_path):
        return AppConfig()

    with open(full_path, 'r', encoding='utf-8') as f:
        raw_config = yaml.safe_load(f)

    if not raw_config:
        return AppConfig()

    config_data = raw_config.get('gateway', {})
    gateway_config = _dict_to_obj(config_data, GatewayConfig)

    ha_data = raw_config.get('ha', {})
    ha_config = _dict_to_obj(ha_data, HAConfig)

    audit_data = raw_config.get('audit', {})
    audit_config = _dict_to_obj(audit_data, AuditConfig)

    sampling_data = raw_config.get('sampling', {})
    sampling_config = _dict_to_obj(sampling_data, SamplingConfig)

    interception_data = raw_config.get('interception', {})
    interception_config = _dict_to_obj(interception_data, InterceptionConfig)

    kafka_data = raw_config.get('sinks', {}).get('kafka', {})
    kafka_config = _dict_to_obj(kafka_data, KafkaSinkConfig)

    es_data = raw_config.get('sinks', {}).get('elasticsearch', {})
    es_config = _dict_to_obj(es_data, ElasticsearchSinkConfig)
    sinks_config = SinksConfig(kafka=kafka_config, elasticsearch=es_config)

    dashboard_data = raw_config.get('dashboard', {})
    dashboard_config = _dict_to_obj(dashboard_data, DashboardConfig)

    otel_data = raw_config.get('otel', {})
    otel_config = _dict_to_obj(otel_data, OtelConfig)

    virus_scan_data = raw_config.get('virus_scan', {})
    clamav_data = virus_scan_data.get('clamav', {})
    clamav_config = _dict_to_obj(clamav_data, ClamAVConfig)
    yara_data = virus_scan_data.get('yara', {})
    yara_config = _dict_to_obj(yara_data, YARAConfig)
    virus_scan_data_clean = {k: v for k, v in virus_scan_data.items() if k not in ('clamav', 'yara')}
    virus_scan_config = _dict_to_obj(virus_scan_data_clean, VirusScanConfig)
    virus_scan_config.clamav = clamav_config
    virus_scan_config.yara = yara_config

    return AppConfig(
        gateway=gateway_config,
        ha=ha_config,
        audit=audit_config,
        sampling=sampling_config,
        interception=interception_config,
        sinks=sinks_config,
        dashboard=dashboard_config,
        otel=otel_config,
        virus_scan=virus_scan_config
    )


_config: Optional[AppConfig] = None


def get_config() -> AppConfig:
    global _config
    if _config is None:
        _config = load_config()
    return _config


def reload_config(config_path: Optional[str] = None) -> AppConfig:
    global _config
    _config = load_config(config_path)
    return _config
