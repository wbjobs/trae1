import logging
import time
from datetime import datetime, timedelta
from typing import List, Optional, Dict, Any

from elasticsearch import AsyncElasticsearch
from elasticsearch.helpers import async_bulk

from .config import ElasticsearchConfig
from .event_models import FileOperationEvent

logger = logging.getLogger(__name__)


INDEX_TEMPLATE = {
    "settings": {
        "number_of_shards": 1,
        "number_of_replicas": 0,
        "index.lifecycle.name": "nas_audit_ilm",
        "index.lifecycle.rollover_alias": "",
    },
    "mappings": {
        "properties": {
            "operation_type": {"type": "keyword"},
            "file_path": {"type": "keyword"},
            "old_file_path": {"type": "keyword"},
            "timestamp": {"type": "date"},
            "@timestamp": {"type": "date"},
            "username": {"type": "keyword"},
            "source_ip": {"type": "keyword"},
            "file_size": {"type": "long"},
            "file_extension": {"type": "keyword"},
        }
    },
}


class ElasticsearchStore:
    def __init__(self, config: ElasticsearchConfig):
        self.config = config
        self._client: Optional[AsyncElasticsearch] = None

    async def connect(self):
        auth = None
        if self.config.username and self.config.password:
            auth = (self.config.username, self.config.password)

        self._client = AsyncElasticsearch(
            hosts=self.config.hosts,
            basic_auth=auth,
            verify_certs=False,
        )
        logger.info(f"Connecting to Elasticsearch at {self.config.hosts}")

        try:
            info = await self._client.info()
            logger.info(f"Connected to Elasticsearch: {info.get('version', {}).get('number', 'unknown')}")
            await self._setup_index_template()
            await self._setup_ilm_policy()
        except Exception as e:
            logger.error(f"Elasticsearch connection error: {e}")
            raise

    async def close(self):
        if self._client:
            await self._client.close()
            self._client = None
            logger.info("Elasticsearch connection closed")

    async def _setup_index_template(self):
        template_name = f"{self.config.index_prefix}_template"
        index_pattern = f"{self.config.index_prefix}-*"

        template = {
            "index_patterns": [index_pattern],
            "template": {
                "settings": INDEX_TEMPLATE["settings"],
                "mappings": INDEX_TEMPLATE["mappings"],
            },
            "priority": 500,
        }

        try:
            await self._client.indices.put_index_template(name=template_name, body=template)
            logger.info(f"Index template '{template_name}' created/updated")
        except Exception as e:
            logger.error(f"Failed to create index template: {e}")

    async def _setup_ilm_policy(self):
        policy_name = "nas_audit_ilm"
        retention_days = self.config.retention_days

        policy = {
            "policy": {
                "phases": {
                    "hot": {
                        "min_age": "0ms",
                        "actions": {
                            "rollover": {
                                "max_age": "1d",
                                "max_size": "50gb",
                            },
                            "set_priority": {
                                "priority": 100,
                            },
                        },
                    },
                    "delete": {
                        "min_age": f"{retention_days}d",
                        "actions": {
                            "delete": {},
                        },
                    },
                }
            }
        }

        try:
            await self._client.ilm.put_lifecycle(name=policy_name, body=policy)
            logger.info(f"ILM policy '{policy_name}' created (retention: {retention_days} days)")
        except Exception as e:
            logger.error(f"Failed to create ILM policy: {e}")

    def _get_write_index(self) -> str:
        date_str = datetime.utcnow().strftime("%Y.%m.%d")
        return f"{self.config.index_prefix}-{date_str}"

    async def index_event(self, event: FileOperationEvent) -> Optional[str]:
        if not self._client:
            return None

        doc = event.to_dict()
        write_index = self._get_write_index()

        try:
            result = await self._client.index(
                index=write_index,
                document=doc,
                refresh=False,
            )
            logger.debug(f"Indexed event: {result['_id']}")
            return result["_id"]
        except Exception as e:
            logger.error(f"Failed to index event: {e}")
            return None

    async def index_events_bulk(self, events: List[FileOperationEvent]) -> int:
        if not self._client or not events:
            return 0

        write_index = self._get_write_index()
        actions = []

        for event in events:
            doc = event.to_dict()
            actions.append({
                "_index": write_index,
                "_source": doc,
            })

        try:
            success, errors = await async_bulk(
                self._client,
                actions,
                raise_on_error=False,
                stats_only=True,
            )
            logger.info(f"Bulk indexed: {success} succeeded, {errors} failed")
            return success
        except Exception as e:
            logger.error(f"Bulk index error: {e}")
            return 0

    async def query_events(
        self,
        username: Optional[str] = None,
        start_time: Optional[float] = None,
        end_time: Optional[float] = None,
        extensions: Optional[List[str]] = None,
        operation_type: Optional[str] = None,
        file_path: Optional[str] = None,
        source_ip: Optional[str] = None,
        size: int = 100,
        scroll_id: Optional[str] = None,
    ) -> Dict[str, Any]:
        if not self._client:
            return {"total": 0, "events": [], "scroll_id": None}

        must = []

        if start_time or end_time:
            range_query = {}
            if start_time:
                range_query["gte"] = datetime.utcfromtimestamp(start_time).strftime("%Y-%m-%dT%H:%M:%S")
            if end_time:
                range_query["lte"] = datetime.utcfromtimestamp(end_time).strftime("%Y-%m-%dT%H:%M:%S")
            must.append({"range": {"@timestamp": range_query}})

        if username:
            must.append({"term": {"username": username}})

        if extensions:
            should = [{"term": {"file_extension": ext.lower()}} for ext in extensions]
            must.append({"bool": {"should": should}})

        if operation_type:
            must.append({"term": {"operation_type": operation_type}})

        if file_path:
            must.append({"wildcard": {"file_path": f"*{file_path}*"}})

        if source_ip:
            must.append({"term": {"source_ip": source_ip}})

        query = {"bool": {"must": must}} if must else {"match_all": {}}

        try:
            if scroll_id:
                result = await self._client.scroll(
                    scroll_id=scroll_id,
                    scroll="2m",
                )
            else:
                result = await self._client.search(
                    index=f"{self.config.index_prefix}-*",
                    query=query,
                    size=size,
                    sort=[{"@timestamp": {"order": "desc"}}],
                    scroll="2m",
                )

            events = []
            for hit in result.get("hits", {}).get("hits", []):
                src = hit["_source"]
                src["_id"] = hit["_id"]
                events.append(src)

            total = result.get("hits", {}).get("total", {}).get("value", 0)
            new_scroll_id = result.get("_scroll_id", None)

            return {
                "total": total,
                "events": events,
                "scroll_id": new_scroll_id,
            }
        except Exception as e:
            logger.error(f"Query error: {e}")
            return {"total": 0, "events": [], "scroll_id": None}

    async def get_top_users(self, top_n: int = 10, days: int = 7) -> List[Dict]:
        if not self._client:
            return []

        start_date = (datetime.utcnow() - timedelta(days=days)).strftime("%Y-%m-%dT%H:%M:%S")

        query = {
            "bool": {
                "must": [
                    {"range": {"@timestamp": {"gte": start_date}}},
                ],
            }
        }

        aggs = {
            "top_users": {
                "terms": {"field": "username", "size": top_n},
            }
        }

        try:
            result = await self._client.search(
                index=f"{self.config.index_prefix}-*",
                query=query,
                aggs=aggs,
                size=0,
            )

            buckets = result.get("aggregations", {}).get("top_users", {}).get("buckets", [])
            return [{"username": b["key"], "count": b["doc_count"]} for b in buckets]
        except Exception as e:
            logger.error(f"Top users query error: {e}")
            return []

    async def get_operation_trend(self, days: int = 7) -> List[Dict]:
        if not self._client:
            return []

        start_date = (datetime.utcnow() - timedelta(days=days)).strftime("%Y-%m-%dT%H:%M:%S")

        query = {
            "bool": {
                "must": [
                    {"range": {"@timestamp": {"gte": start_date}}},
                ],
            }
        }

        aggs = {
            "by_day": {
                "date_histogram": {
                    "field": "@timestamp",
                    "calendar_interval": "day",
                    "format": "yyyy-MM-dd",
                },
                "aggs": {
                    "by_operation": {
                        "terms": {"field": "operation_type", "size": 10},
                    }
                },
            }
        }

        try:
            result = await self._client.search(
                index=f"{self.config.index_prefix}-*",
                query=query,
                aggs=aggs,
                size=0,
            )

            buckets = result.get("aggregations", {}).get("by_day", {}).get("buckets", [])
            trend = []
            for bucket in buckets:
                day_data = {
                    "date": bucket["key_as_string"],
                    "total": bucket["doc_count"],
                    "by_operation": {
                        op_b["key"]: op_b["doc_count"]
                        for op_b in bucket.get("by_operation", {}).get("buckets", [])
                    },
                }
                trend.append(day_data)
            return trend
        except Exception as e:
            logger.error(f"Trend query error: {e}")
            return []

    async def get_extension_stats(self, days: int = 7) -> List[Dict]:
        if not self._client:
            return []

        start_date = (datetime.utcnow() - timedelta(days=days)).strftime("%Y-%m-%dT%H:%M:%S")

        query = {
            "bool": {
                "must": [
                    {"range": {"@timestamp": {"gte": start_date}}},
                ],
            }
        }

        aggs = {
            "by_extension": {
                "terms": {"field": "file_extension", "size": 20},
            }
        }

        try:
            result = await self._client.search(
                index=f"{self.config.index_prefix}-*",
                query=query,
                aggs=aggs,
                size=0,
            )

            buckets = result.get("aggregations", {}).get("by_extension", {}).get("buckets", [])
            return [{"extension": b["key"], "count": b["doc_count"]} for b in buckets]
        except Exception as e:
            logger.error(f"Extension stats query error: {e}")
            return []

    async def cleanup_old_indices(self):
        if not self._client:
            return

        retention_days = self.config.retention_days
        cutoff_date = (datetime.utcnow() - timedelta(days=retention_days)).strftime("%Y.%m.%d")

        try:
            indices = await self._client.indices.get_alias(index=f"{self.config.index_prefix}-*")
            deleted_count = 0
            for idx_name in indices:
                try:
                    date_part = idx_name.replace(f"{self.config.index_prefix}-", "")
                    if date_part < cutoff_date:
                        await self._client.indices.delete(index=idx_name)
                        deleted_count += 1
                        logger.info(f"Deleted old index: {idx_name}")
                except Exception:
                    continue

            logger.info(f"Cleanup complete: {deleted_count} old indices deleted")
        except Exception as e:
            logger.error(f"Cleanup error: {e}")
