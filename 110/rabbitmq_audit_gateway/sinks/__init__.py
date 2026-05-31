"""Audit log sinks (Kafka, Elasticsearch)"""
import json
import gzip
import logging
from abc import ABC, abstractmethod
from typing import List, Optional, Dict, Any
from datetime import datetime

try:
    from kafka import KafkaProducer
    from kafka.errors import KafkaError
    KAFKA_AVAILABLE = True
except ImportError:
    KAFKA_AVAILABLE = False

try:
    from elasticsearch import Elasticsearch
    ES_AVAILABLE = True
except ImportError:
    ES_AVAILABLE = False

from ..audit import AuditBatch, AuditRecord


logger = logging.getLogger(__name__)


class SinkBase(ABC):
    @abstractmethod
    def send(self, record: AuditRecord) -> bool:
        pass

    @abstractmethod
    def send_batch(self, batch: AuditBatch) -> bool:
        pass

    @abstractmethod
    def close(self) -> None:
        pass


class KafkaSink(SinkBase):
    def __init__(self, config):
        if not KAFKA_AVAILABLE:
            raise ImportError("kafka-python is not installed. Install with: pip install kafka-python")

        self.config = config
        self._bootstrap_servers = config.kafka.bootstrap_servers
        self._topic = config.kafka.topic
        self._compression = config.kafka.compression
        self._enabled = config.kafka.enabled
        self._producer: Optional[KafkaProducer] = None
        self._connect()

    def _connect(self) -> None:
        if not self._enabled:
            return

        try:
            self._producer = KafkaProducer(
                bootstrap_servers=self._bootstrap_servers,
                compression_type=self._compression,
                value_serializer=lambda v: gzip.compress(json.dumps(v).encode('utf-8')),
                key_serializer=lambda k: k.encode('utf-8') if k else None,
                acks='all',
                retries=3,
                max_in_flight_requests_per_connection=1
            )
            logger.info(f"Kafka producer connected to {self._bootstrap_servers}")
        except Exception as e:
            logger.error(f"Failed to connect to Kafka: {e}")
            self._producer = None

    def send(self, record: AuditRecord) -> bool:
        if not self._enabled or not self._producer:
            return False

        try:
            future = self._producer.send(
                self._topic,
                key=record.message_id,
                value=record.to_dict()
            )
            future.get(timeout=10)
            return True
        except Exception as e:
            logger.error(f"Failed to send record to Kafka: {e}")
            return False

    def send_batch(self, batch: AuditBatch) -> bool:
        if not self._enabled or not self._producer:
            return False

        try:
            for record in batch.records:
                self._producer.send(
                    self._topic,
                    key=record.message_id,
                    value=record.to_dict()
                )
            self._producer.flush()
            return True
        except Exception as e:
            logger.error(f"Failed to send batch to Kafka: {e}")
            return False

    def close(self) -> None:
        if self._producer:
            self._producer.close()
            self._producer = None


class ElasticsearchSink(SinkBase):
    def __init__(self, config):
        if not ES_AVAILABLE:
            raise ImportError("elasticsearch is not installed. Install with: pip install elasticsearch")

        self.config = config
        self._hosts = config.elasticsearch.hosts
        self._index = config.elasticsearch.index
        self._username = config.elasticsearch.username
        self._password = config.elasticsearch.password
        self._enabled = config.elasticsearch.enabled
        self._client: Optional[Elasticsearch] = None
        self._connect()

    def _connect(self) -> None:
        if not self._enabled:
            return

        try:
            self._client = Elasticsearch(
                self._hosts,
                http_auth=(self._username, self._password),
                use_ssl=True if 'https' in self._hosts[0] else False,
                verify_certs=False,
                timeout=30,
                max_retries=3,
                retry_on_timeout=True
            )
            if self._client.ping():
                logger.info(f"Elasticsearch connected to {self._hosts}")
                self._ensure_index()
            else:
                logger.warning("Elasticsearch ping failed")
                self._client = None
        except Exception as e:
            logger.error(f"Failed to connect to Elasticsearch: {e}")
            self._client = None

    def _ensure_index(self) -> None:
        if not self._client:
            return

        try:
            if not self._client.indices.exists(index=self._index):
                mapping = {
                    "mappings": {
                        "properties": {
                            "message_id": {"type": "keyword"},
                            "exchange": {"type": "keyword"},
                            "routing_key": {"type": "keyword"},
                            "timestamp": {"type": "date"},
                            "producer_ip": {"type": "ip"},
                            "consumer_ip": {"type": "ip"},
                            "message_body_size": {"type": "long"},
                            "direction": {"type": "keyword"},
                            "action": {"type": "keyword"},
                            "headers": {"type": "object", "enabled": False},
                            "body_preview": {"type": "text"},
                            "intercepted": {"type": "boolean"},
                            "interception_rule_id": {"type": "keyword"},
                            "interception_reason": {"type": "text"},
                            "sampled": {"type": "boolean"},
                            "latency_ms": {"type": "float"},
                            "gateway_instance_id": {"type": "keyword"},
                            "record_time": {"type": "date"}
                        }
                    },
                    "settings": {
                        "number_of_shards": 1,
                        "number_of_replicas": 1
                    }
                }
                self._client.indices.create(index=self._index, body=mapping)
                logger.info(f"Created Elasticsearch index: {self._index}")
        except Exception as e:
            logger.error(f"Failed to ensure Elasticsearch index: {e}")

    def send(self, record: AuditRecord) -> bool:
        if not self._enabled or not self._client:
            return False

        try:
            doc_id = record.message_id
            self._client.index(
                index=self._index,
                id=doc_id,
                document=record.to_dict()
            )
            return True
        except Exception as e:
            logger.error(f"Failed to send record to Elasticsearch: {e}")
            return False

    def send_batch(self, batch: AuditBatch) -> bool:
        if not self._enabled or not self._client:
            return False

        try:
            operations = []
            for record in batch.records:
                operations.append({"index": {"_index": self._index, "_id": record.message_id}})
                operations.append(record.to_dict())

            if operations:
                self._client.bulk(operations=operations, refresh=True)
            return True
        except Exception as e:
            logger.error(f"Failed to send batch to Elasticsearch: {e}")
            return False

    def close(self) -> None:
        if self._client:
            self._client.close()
            self._client = None

    def search(self, query: Dict[str, Any], size: int = 100) -> List[Dict[str, Any]]:
        if not self._enabled or not self._client:
            return []

        try:
            response = self._client.search(index=self._index, query=query, size=size)
            return [hit["_source"] for hit in response["hits"]["hits"]]
        except Exception as e:
            logger.error(f"Failed to search Elasticsearch: {e}")
            return []


class ConsoleSink(SinkBase):
    def __init__(self, config=None):
        self._enabled = True

    def send(self, record: AuditRecord) -> bool:
        if not self._enabled:
            return False
        print(f"[AUDIT] {record.to_json()}")
        return True

    def send_batch(self, batch: AuditBatch) -> bool:
        if not self._enabled:
            return False
        for record in batch.records:
            print(f"[AUDIT] {record.to_json()}")
        return True

    def close(self) -> None:
        pass


def create_sinks(config) -> List[SinkBase]:
    sinks = []

    if config.sinks.kafka.enabled:
        try:
            sinks.append(KafkaSink(config))
        except ImportError as e:
            logger.warning(f"Kafka sink not available: {e}")

    if config.sinks.elasticsearch.enabled:
        try:
            sinks.append(ElasticsearchSink(config))
        except ImportError as e:
            logger.warning(f"Elasticsearch sink not available: {e}")

    if not sinks:
        sinks.append(ConsoleSink(config))

    return sinks
