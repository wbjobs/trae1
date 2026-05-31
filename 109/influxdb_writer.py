import logging
import time
from typing import Dict, Any
from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

logger = logging.getLogger(__name__)

class InfluxDBWriter:
    def __init__(self, config):
        self.config = config
        self.client = None
        self.write_api = None
        self.connected = False
        self._connect()
    
    def _connect(self):
        if self.config.dry_run:
            logger.info("Dry-run mode: skipping InfluxDB connection")
            return
        
        try:
            self.client = InfluxDBClient(
                url=self.config.influxdb_url,
                token=self.config.influxdb_token,
                org=self.config.influxdb_org
            )
            self.write_api = self.client.write_api(write_options=SYNCHRONOUS)
            self.connected = True
            logger.info("Connected to InfluxDB")
        except Exception as e:
            logger.error("Failed to connect to InfluxDB", extra={"error": str(e)})
    
    async def write_diversion_event(self, rule, attack_type: str, status: str):
        if self.config.dry_run:
            logger.info("Dry-run mode: would write to InfluxDB", extra={
                "rule_id": rule.rule_id,
                "attack_type": attack_type,
                "status": status
            })
            return
        
        if not self.connected:
            return
        
        try:
            point = (
                Point("diversion_events")
                .tag("rule_id", rule.rule_id)
                .tag("attack_type", attack_type)
                .tag("status", status)
                .tag("src_ip", rule.src_ip or "any")
                .tag("dst_ip", rule.dst_ip or "any")
                .tag("protocol", str(rule.protocol or "any"))
                .tag("action", rule.action)
                .field("src_port", rule.src_port or 0)
                .field("dst_port", rule.dst_port or 0)
                .time(int(time.time() * 1000000000), WritePrecision.NS)
            )
            self.write_api.write(
                bucket=self.config.influxdb_bucket,
                org=self.config.influxdb_org,
                record=point
            )
            logger.debug("Wrote diversion event to InfluxDB", extra={"rule_id": rule.rule_id})
        except Exception as e:
            logger.error("Failed to write to InfluxDB", extra={"error": str(e)})
    
    async def write_traffic_stats(self, stats: Dict[str, Any]):
        if self.config.dry_run or not self.connected:
            return
        
        try:
            for dst_ip, bytes_count in stats.get("current_traffic", {}).items():
                point = (
                    Point("traffic_stats")
                    .tag("dst_ip", dst_ip)
                    .field("bytes", bytes_count)
                    .time(int(time.time() * 1000000000), WritePrecision.NS)
                )
                self.write_api.write(
                    bucket=self.config.influxdb_bucket,
                    org=self.config.influxdb_org,
                    record=point
                )
        except Exception as e:
            logger.error("Failed to write traffic stats", extra={"error": str(e)})
    
    def close(self):
        if self.write_api:
            self.write_api.close()
        if self.client:
            self.client.close()
