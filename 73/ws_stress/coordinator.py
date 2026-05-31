"""Redis 分布式任务协调器 - 多节点任务分配与结果聚合"""

import asyncio
import json
import time
import uuid
from typing import Optional, Tuple

import redis.asyncio as aioredis

from .config import ClientConfig
from .stats import StatisticsCollector


REDIS_PREFIX = "ws_stress"
TASK_CHANNEL = f"{REDIS_PREFIX}:task"
RESULT_KEY = f"{REDIS_PREFIX}:results"
REGISTER_KEY = f"{REDIS_PREFIX}:nodes"
READY_KEY = f"{REDIS_PREFIX}:ready"
START_KEY = f"{REDIS_PREFIX}:start"


class RedisCoordinator:
    def __init__(self, redis_url: str, node_id: str):
        self._redis_url = redis_url
        self._node_id = node_id
        self._redis: Optional[aioredis.Redis] = None
        self._pubsub: Optional[aioredis.client.PubSub] = None
        self._master_ready_event = asyncio.Event()
        self._start_event = asyncio.Event()

    async def connect(self) -> bool:
        try:
            self._redis = aioredis.from_url(
                self._redis_url,
                socket_timeout=5,
                socket_connect_timeout=5,
            )
            await self._redis.ping()
            return True
        except Exception:
            return False

    async def register_node(self, total_clients: int) -> bool:
        try:
            await self._redis.hset(REGISTER_KEY, self._node_id, str(total_clients))
            return True
        except Exception:
            return False

    async def unregister_node(self):
        try:
            await self._redis.hdel(REGISTER_KEY, self._node_id)
        except Exception:
            pass

    async def get_all_nodes(self) -> dict:
        try:
            nodes = await self._redis.hgetall(REGISTER_KEY)
            return {k.decode(): int(v) for k, v in nodes.items()}
        except Exception:
            return {}

    async def wait_for_all_nodes(self, expected_count: int, timeout: float = 60) -> bool:
        deadline = time.time() + timeout
        while time.time() < deadline:
            nodes = await self.get_all_nodes()
            if len(nodes) >= expected_count:
                return True
            await asyncio.sleep(1)
        return False

    async def signal_ready(self):
        await self._redis.hset(READY_KEY, self._node_id, "1")

    async def wait_for_all_ready(self, timeout: float = 120) -> bool:
        deadline = time.time() + timeout
        nodes = await self.get_all_nodes()
        expected = set(nodes.keys())
        while time.time() < deadline:
            ready_nodes = await self._redis.hgetall(READY_KEY)
            ready_set = {k.decode() for k in ready_nodes.keys()}
            if expected.issubset(ready_set):
                return True
            await asyncio.sleep(1)
        return False

    async def broadcast_start(self):
        await self._redis.set(START_KEY, str(time.time()))
        await self._redis.publish(TASK_CHANNEL, f"start:{time.time()}")

    async def wait_for_start(self, timeout: float = 300) -> bool:
        deadline = time.time() + timeout
        while time.time() < deadline:
            val = await self._redis.get(START_KEY)
            if val:
                return True
            await asyncio.sleep(0.5)
        return False

    async def submit_result(self, report_dict: dict):
        try:
            report_dict["node_id"] = self._node_id
            report_dict["submitted_at"] = time.time()
            await self._redis.hset(RESULT_KEY, self._node_id, json.dumps(report_dict))
        except Exception:
            pass

    async def collect_results(self) -> list:
        try:
            results = await self._redis.hgetall(RESULT_KEY)
            return [json.loads(v) for v in results.values()]
        except Exception:
            return []

    async def compute_client_assignment(self, total_clients: int) -> Tuple[int, int]:
        nodes = await self.get_all_nodes()
        node_ids = sorted(nodes.keys())
        if not node_ids:
            return 0, total_clients
        idx = node_ids.index(self._node_id)
        total_nodes = len(node_ids)
        base = total_clients // total_nodes
        remainder = total_clients % total_nodes
        start = idx * base + min(idx, remainder)
        count = base + (1 if idx < remainder else 0)
        return start, count

    async def close(self):
        try:
            if self._pubsub:
                await self._pubsub.close()
            if self._redis:
                await self._redis.close()
        except Exception:
            pass
