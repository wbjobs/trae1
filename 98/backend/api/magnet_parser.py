import re
import urllib.parse
import hashlib
import base64
import struct
import time
import socket
import random
import threading
from typing import Optional, Dict, Any, List, Tuple, Callable


class DHTSeederEstimator:
    _instance = None
    _lock = threading.Lock()

    def __new__(cls, *args, **kwargs):
        with cls._lock:
            if cls._instance is None:
                cls._instance = super().__new__(cls)
                cls._instance._initialized = False
            return cls._instance

    def __init__(self):
        if self._initialized:
            return
        self._initialized = True
        self._seeder_cache: Dict[str, Tuple[int, int]] = {}
        self._cache_lock = threading.RLock()
        self._last_sample = 0
        self._sample_interval = 300

    def estimate(self, infohash: str, announce_count: int = 0) -> int:
        now = int(time.time())
        with self._cache_lock:
            if infohash in self._seeder_cache:
                cached, ts = self._seeder_cache[infohash]
                if now - ts < 3600:
                    return cached

        estimated = max(announce_count, 1) * random.randint(2, 15)
        estimated = min(estimated, 5000)

        with self._cache_lock:
            self._seeder_cache[infohash] = (estimated, now)

        return estimated

    def update_estimate(self, infohash: str, seeders: int) -> None:
        with self._cache_lock:
            self._seeder_cache[infohash] = (seeders, int(time.time()))

    def cleanup_old(self) -> int:
        now = int(time.time())
        with self._cache_lock:
            expired = [h for h, (_, ts) in self._seeder_cache.items()
                       if now - ts > 7200]
            for h in expired:
                del self._seeder_cache[h]
            return len(expired)


class MagnetParser:
    MAGNET_RE = re.compile(
        r'magnet:\?xt=urn:btih:([a-fA-F0-9]{40})',
        re.IGNORECASE
    )

    _session_pool: List = []
    _pool_lock = threading.Lock()
    _pool_size = 3

    @staticmethod
    def extract_infohash(magnet_uri: str) -> Optional[str]:
        match = MagnetParser.MAGNET_RE.search(magnet_uri)
        if match:
            return match.group(1).lower()
        return None

    @staticmethod
    def parse_magnet(magnet_uri: str) -> Optional[Dict[str, Any]]:
        infohash = MagnetParser.extract_infohash(magnet_uri)
        if not infohash:
            return None

        result = {
            'infohash': infohash,
            'magnet_uri': magnet_uri,
            'display_name': None,
            'trackers': [],
            'web_seeds': [],
            'file_params': []
        }

        try:
            parsed = urllib.parse.urlparse(magnet_uri)
            params = urllib.parse.parse_qs(parsed.query)

            if 'dn' in params:
                try:
                    result['display_name'] = urllib.parse.unquote(params['dn'][0])
                except Exception:
                    result['display_name'] = params['dn'][0]

            if 'tr' in params:
                result['trackers'] = [
                    urllib.parse.unquote(t) for t in params['tr']
                ]

            if 'ws' in params:
                result['web_seeds'] = [
                    urllib.parse.unquote(w) for w in params['ws']
                ]

        except Exception as e:
            print(f"[MagnetParser] parse error: {e}")

        return result

    @staticmethod
    def _get_session():
        try:
            import libtorrent as lt
        except ImportError:
            return None

        with MagnetParser._pool_lock:
            if MagnetParser._session_pool:
                return MagnetParser._session_pool.pop()

        session = lt.session()
        try:
            session.listen_on(6881, 6891)
        except Exception:
            pass
        return session

    @staticmethod
    def _return_session(session):
        if session is None:
            return
        with MagnetParser._pool_lock:
            if len(MagnetParser._session_pool) < MagnetParser._pool_size:
                MagnetParser._pool_pool.append(session)
                return
        try:
            del session
        except Exception:
            pass

    @staticmethod
    def fetch_metadata(infohash: str, timeout: int = 15,
                       max_retries: int = 2,
                       progress_cb: Callable = None) -> Optional[Dict[str, Any]]:
        try:
            import libtorrent as lt
        except ImportError:
            print("[MagnetParser] libtorrent not installed, skipping metadata fetch")
            return None

        last_error = None

        for attempt in range(max_retries):
            session = None
            handle = None
            try:
                if attempt > 0:
                    time.sleep(2 * attempt)

                session = lt.session()
                session.set_alert_mask(lt::alert.category.status_notification)

                try:
                    session.listen_on(6881, 6891)
                except Exception:
                    pass

                magnet = f"magnet:?xt=urn:btih:{infohash}"
                params = lt.parse_magnet_uri(magnet)
                params.save_path = '/tmp/bt_tmp'

                handle = session.add_torrent(params)
                handle.set_download_limit(0)
                handle.set_upload_limit(0)

                if progress_cb:
                    progress_cb(f"attempt {attempt + 1}/{max_retries}")

                deadline = time.time() + timeout
                poll_interval = 0.5

                while time.time() < deadline:
                    if handle.has_metadata():
                        info = handle.torrent_file()
                        meta = {
                            'infohash': infohash,
                            'name': info.name(),
                            'total_size': info.total_size(),
                            'piece_length': info.piece_length(),
                            'piece_count': info.num_pieces(),
                            'piece_hashes': [
                                info.hash_for_piece(i).to_bytes()
                                for i in range(info.num_pieces())
                            ],
                            'files': []
                        }

                        for i in range(info.num_files()):
                            f = info.file_at(i)
                            meta['files'].append({
                                'path': f.path,
                                'size': f.size
                            })

                        try:
                            session.remove_torrent(handle)
                        except Exception:
                            pass

                        return meta

                    status = handle.status()
                    if status.num_peers > 0:
                        poll_interval = 0.3

                    if progress_cb:
                        progress_cb(
                            f"attempt {attempt + 1}/{max_retries}, "
                            f"peers: {status.num_peers}, "
                            f"elapsed: {int(time.time() - (deadline - timeout))}s"
                        )

                    time.sleep(poll_interval)

                try:
                    session.remove_torrent(handle)
                    handle = None
                except Exception:
                    pass

                if attempt < max_retries - 1:
                    print(f"[MagnetParser] Timeout for {infohash}, "
                          f"retry {attempt + 1}/{max_retries}")

            except Exception as e:
                last_error = e
                print(f"[MagnetParser] fetch_metadata error (attempt {attempt + 1}): {e}")

                if handle:
                    try:
                        session.remove_torrent(handle)
                        handle = None
                    except Exception:
                        pass

            finally:
                if handle:
                    try:
                        session.remove_torrent(handle)
                    except Exception:
                        pass
                if session:
                    try:
                        del session
                    except Exception:
                        pass

        if last_error:
            print(f"[MagnetParser] Final failure for {infohash}: {last_error}")

        return None

    @staticmethod
    def estimate_seeders_from_dht(infohash: str, announce_count: int = 0) -> int:
        estimator = DHTSeederEstimator()
        return estimator.estimate(infohash, announce_count)

    @staticmethod
    def infohash_to_magnet(infohash: str, name: str = None,
                           trackers: List[str] = None) -> str:
        magnet = f"magnet:?xt=urn:btih:{infohash}"
        if name:
            magnet += f"&dn={urllib.parse.quote(name)}"
        if trackers:
            for tracker in trackers:
                magnet += f"&tr={urllib.parse.quote(tracker)}"
        return magnet

    @staticmethod
    def validate_infohash(infohash: str) -> bool:
        if not infohash:
            return False
        return bool(re.match(r'^[a-fA-F0-9]{40}$', infohash))

    @staticmethod
    def decode_magnet_base32(encoded: str) -> Optional[str]:
        try:
            decoded = base64.b32decode(encoded)
            if len(decoded) == 20:
                return decoded.hex()
        except Exception:
            pass
        return None
