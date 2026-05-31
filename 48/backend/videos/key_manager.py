import os
import json
import time
import base64
import hmac
import hashlib
import secrets
from datetime import datetime, timedelta, timezone as dt_timezone
from typing import Optional, Tuple, List

import redis
from django.conf import settings


class KeyManager:
    KEY_PREFIX = 'hls:key'
    INDEX_KEY = 'hls:key:current_index'
    KEY_VERSION_PREFIX = 'hls:key:version'
    TOKEN_PREFIX = 'hls:token'

    def __init__(self):
        self._redis = None

    @property
    def redis(self):
        if self._redis is None:
            self._redis = redis.Redis.from_url(settings.REDIS_URL, decode_responses=True)
        return self._redis

    def _get_current_index(self) -> int:
        idx = self.redis.get(self.INDEX_KEY)
        if idx is None:
            self.redis.set(self.INDEX_KEY, 0)
            return 0
        return int(idx)

    def _increment_index(self) -> int:
        return self.redis.incr(self.INDEX_KEY)

    def generate_key(self) -> Tuple[str, bytes]:
        key_id = f'key_{int(time.time())}_{secrets.token_hex(4)}'
        key_bytes = secrets.token_bytes(16)
        return key_id, key_bytes

    def get_or_create_current_key(self) -> Tuple[str, bytes, int]:
        current_idx = self._get_current_index()
        cache_key = f'{self.KEY_PREFIX}:{current_idx}'

        data = self.redis.get(cache_key)
        if data:
            key_info = json.loads(data)
            key_id = key_info['key_id']
            key_bytes = base64.b64decode(key_info['key_b64'])
            return key_id, key_bytes, current_idx

        key_id, key_bytes = self.generate_key()
        key_info = {
            'key_id': key_id,
            'key_b64': base64.b64encode(key_bytes).decode('utf-8'),
            'index': current_idx,
            'created_at': datetime.now(dt_timezone.utc).isoformat(),
        }
        rotation_interval = settings.HLS_ENCRYPTION_KEY_ROTATION_INTERVAL
        grace_period = settings.HLS_ENCRYPTION_KEY_GRACE_PERIOD
        self.redis.setex(cache_key, rotation_interval + grace_period, json.dumps(key_info))

        version_key = f'{self.KEY_VERSION_PREFIX}:{current_idx}'
        self.redis.setex(version_key, rotation_interval + grace_period, json.dumps(key_info))

        return key_id, key_bytes, current_idx

    def rotate_key(self) -> Tuple[str, bytes, int]:
        new_index = self._increment_index()
        key_id, key_bytes = self.generate_key()
        key_info = {
            'key_id': key_id,
            'key_b64': base64.b64encode(key_bytes).decode('utf-8'),
            'index': new_index,
            'created_at': datetime.now(dt_timezone.utc).isoformat(),
        }
        rotation_interval = settings.HLS_ENCRYPTION_KEY_ROTATION_INTERVAL
        grace_period = settings.HLS_ENCRYPTION_KEY_GRACE_PERIOD
        ttl = rotation_interval + grace_period

        cache_key = f'{self.KEY_PREFIX}:{new_index}'
        self.redis.setex(cache_key, ttl, json.dumps(key_info))

        version_key = f'{self.KEY_VERSION_PREFIX}:{new_index}'
        self.redis.setex(version_key, ttl, json.dumps(key_info))

        return key_id, key_bytes, new_index

    def get_key_by_index(self, index: int) -> Tuple[Optional[str], Optional[bytes]]:
        version_key = f'{self.KEY_VERSION_PREFIX}:{index}'
        data = self.redis.get(version_key)
        if data:
            key_info = json.loads(data)
            return key_info['key_id'], base64.b64decode(key_info['key_b64'])
        return None, None

    def get_key_id_by_index(self, index: int) -> Optional[str]:
        version_key = f'{self.KEY_VERSION_PREFIX}:{index}'
        data = self.redis.get(version_key)
        if data:
            key_info = json.loads(data)
            return key_info['key_id']
        return None

    def get_valid_key_range(self) -> Tuple[int, int]:
        current_idx = self._get_current_index()
        rotation_interval = settings.HLS_ENCRYPTION_KEY_ROTATION_INTERVAL
        grace_period = settings.HLS_ENCRYPTION_KEY_GRACE_PERIOD
        max_grace_indices = (grace_period // rotation_interval) + 1
        min_valid_idx = max(0, current_idx - max_grace_indices)
        return min_valid_idx, current_idx

    def get_key_info_for_manifest(self) -> dict:
        current_idx = self._get_current_index()
        key_id, key_bytes, idx = self.get_or_create_current_key()
        min_idx, max_idx = self.get_valid_key_range()
        return {
            'current_key_id': key_id,
            'current_index': current_idx,
            'min_valid_index': min_idx,
            'max_valid_index': max_idx,
        }

    def generate_play_token(self, video_id: str, client_ip: str) -> str:
        token = secrets.token_urlsafe(32)
        ttl = settings.HLS_ANTI_HOTLINK_TTL
        token_data = {
            'video_id': str(video_id),
            'ip': client_ip,
            'created_at': time.time(),
        }
        token_key = f'{self.TOKEN_PREFIX}:{token}'
        self.redis.setex(token_key, ttl, json.dumps(token_data))
        return token

    def validate_play_token(self, token: str, video_id: str, client_ip: str) -> bool:
        token_key = f'{self.TOKEN_PREFIX}:{token}'
        data = self.redis.get(token_key)
        if not data:
            return False
        token_data = json.loads(data)
        if token_data.get('video_id') != str(video_id):
            return False
        if token_data.get('ip') != client_ip:
            return False
        return True

    def consume_play_token(self, token: str) -> bool:
        token_key = f'{self.TOKEN_PREFIX}:{token}'
        return bool(self.redis.delete(token_key))


key_manager = KeyManager()
