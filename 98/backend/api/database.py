import sqlite3
import os
import json
import time
import hashlib
import struct
import threading
from typing import Optional, List, Dict, Any, Set
from contextlib import contextmanager


class BloomFilter:
    def __init__(self, expected_items: int = 1000000, false_positive_rate: float = 0.01):
        self.expected_items = expected_items
        self.false_positive_rate = false_positive_rate
        self.size = self._optimal_size(expected_items, false_positive_rate)
        self.hash_count = self._optimal_hash_count(self.size, expected_items)
        self.bit_array: Set[int] = set()
        self._lock = threading.RLock()

    @staticmethod
    def _optimal_size(n: int, p: float) -> int:
        import math
        m = -(n * math.log(p)) / (math.log(2) ** 2)
        return int(m)

    @staticmethod
    def _optimal_hash_count(m: int, n: int) -> int:
        import math
        k = (m / n) * math.log(2)
        return max(1, int(k))

    def _hashes(self, item: str) -> List[int]:
        hashes = []
        for i in range(self.hash_count):
            h = hashlib.sha256(f"{item}:{i}".encode()).digest()
            pos = int.from_bytes(h[:8], 'big') % self.size
            hashes.append(pos)
        return hashes

    def add(self, item: str) -> None:
        with self._lock:
            for h in self._hashes(item):
                self.bit_array.add(h)

    def __contains__(self, item: str) -> bool:
        with self._lock:
            return all(h in self.bit_array for h in self._hashes(item))

    def clear(self) -> None:
        with self._lock:
            self.bit_array.clear()

    def __len__(self) -> int:
        with self._lock:
            return len(self.bit_array)

    def estimated_count(self) -> int:
        import math
        with self._lock:
            if not self.bit_array:
                return 0
            m = self.size
            k = self.hash_count
            x = len(self.bit_array)
            return int(-(m / k) * math.log(1 - x / m))


class Database:
    BLACKLIST_TTL_SECONDS = 24 * 3600
    METADATA_TIMEOUT = 15
    MAX_RETRY = 2
    FAILURE_RATE_THRESHOLD = 0.5
    COPYRIGHT_THRESHOLD = 0.95

    def __init__(self, db_path: str = None):
        if db_path is None:
            db_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'data', 'bt_monitor.db')
        self.db_path = db_path
        os.makedirs(os.path.dirname(self.db_path), exist_ok=True)

        self.bloom_filter = BloomFilter(expected_items=500000, false_positive_rate=0.001)
        self._blacklist_cache: Dict[str, int] = {}
        self._retry_count: Dict[str, int] = {}
        self._failure_stats = {'success': 0, 'failure': 0}
        self._stats_lock = threading.RLock()

        self._init_db()
        self._load_bloom_filter()

    @contextmanager
    def _get_conn(self):
        conn = sqlite3.connect(self.db_path, timeout=30)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA journal_mode=WAL")
        conn.execute("PRAGMA synchronous=NORMAL")
        conn.execute("PRAGMA busy_timeout=5000")
        try:
            yield conn
            conn.commit()
        except Exception:
            conn.rollback()
            raise
        finally:
            conn.close()

    def _init_db(self):
        with self._get_conn() as conn:
            conn.executescript('''
                CREATE TABLE IF NOT EXISTS infohashes (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    infohash TEXT UNIQUE NOT NULL,
                    source_ip TEXT,
                    source_port INTEGER,
                    first_seen INTEGER,
                    last_seen INTEGER,
                    download_count INTEGER DEFAULT 0,
                    seeders INTEGER DEFAULT 0,
                    leechers INTEGER DEFAULT 0,
                    estimated_seeders INTEGER DEFAULT 0,
                    retry_count INTEGER DEFAULT 0,
                    last_attempt INTEGER DEFAULT 0,
                    status TEXT DEFAULT 'pending'
                );

                CREATE TABLE IF NOT EXISTS torrents (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    infohash TEXT UNIQUE NOT NULL,
                    name TEXT,
                    total_size INTEGER DEFAULT 0,
                    piece_length INTEGER DEFAULT 0,
                    piece_count INTEGER DEFAULT 0,
                    piece_hashes TEXT DEFAULT '[]',
                    files TEXT DEFAULT '[]',
                    parsed_at INTEGER,
                    FOREIGN KEY (infohash) REFERENCES infohashes(infohash)
                );

                CREATE TABLE IF NOT EXISTS download_history (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    infohash TEXT NOT NULL,
                    timestamp INTEGER NOT NULL,
                    FOREIGN KEY (infohash) REFERENCES infohashes(infohash)
                );

                CREATE TABLE IF NOT EXISTS blacklist (
                    infohash TEXT PRIMARY KEY,
                    reason TEXT,
                    added_at INTEGER NOT NULL,
                    expires_at INTEGER NOT NULL
                );

                CREATE TABLE IF NOT EXISTS copyright_fingerprints (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    md5 TEXT UNIQUE,
                    sha1 TEXT,
                    sha256 TEXT,
                    phash TEXT,
                    file_size INTEGER,
                    filename TEXT,
                    title TEXT,
                    copyright_holder TEXT,
                    original_source TEXT,
                    added_at INTEGER,
                    fingerprint_version TEXT
                );

                CREATE TABLE IF NOT EXISTS infringement_records (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    infohash TEXT NOT NULL,
                    file_name TEXT,
                    file_size INTEGER,
                    phash TEXT,
                    matched_fingerprint_id INTEGER,
                    matched_title TEXT,
                    copyright_holder TEXT,
                    similarity REAL,
                    uploader_ip TEXT,
                    source_ip TEXT,
                    detected_at INTEGER,
                    status TEXT DEFAULT 'suspected',
                    dmca_generated INTEGER DEFAULT 0,
                    evidence_json TEXT,
                    FOREIGN KEY (infohash) REFERENCES infohashes(infohash),
                    FOREIGN KEY (matched_fingerprint_id) REFERENCES copyright_fingerprints(id)
                );

                CREATE TABLE IF NOT EXISTS dmca_notices (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    infringement_id INTEGER,
                    infohash TEXT,
                    recipient_email TEXT,
                    subject TEXT,
                    notice_text TEXT,
                    generated_at INTEGER,
                    sent_at INTEGER,
                    status TEXT DEFAULT 'generated',
                    FOREIGN KEY (infringement_id) REFERENCES infringement_records(id)
                );

                CREATE INDEX IF NOT EXISTS idx_infohash_last_seen
                    ON infohashes(last_seen);
                CREATE INDEX IF NOT EXISTS idx_infohash_download_count
                    ON infohashes(download_count);
                CREATE INDEX IF NOT EXISTS idx_infohash_estimated_seeders
                    ON infohashes(estimated_seeders);
                CREATE INDEX IF NOT EXISTS idx_download_history_ts
                    ON download_history(timestamp);
                CREATE INDEX IF NOT EXISTS idx_blacklist_expires
                    ON blacklist(expires_at);
                CREATE INDEX IF NOT EXISTS idx_infringement_infohash
                    ON infringement_records(infohash);
                CREATE INDEX IF NOT EXISTS idx_infringement_holder
                    ON infringement_records(copyright_holder);
                CREATE INDEX IF NOT EXISTS idx_infringement_status
                    ON infringement_records(status);
                CREATE INDEX IF NOT EXISTS idx_fingerprint_phash
                    ON copyright_fingerprints(phash);
                CREATE INDEX IF NOT EXISTS idx_fingerprint_holder
                    ON copyright_fingerprints(copyright_holder);
                CREATE INDEX IF NOT EXISTS idx_dmca_infringement
                    ON dmca_notices(infringement_id);
            ''')

            try:
                conn.execute('''
                    CREATE VIRTUAL TABLE IF NOT EXISTS torrent_fts USING fts5(
                        name,
                        files,
                        infohash UNINDEXED,
                        tokenize='porter unicode61'
                    )
                ''')
            except sqlite3.OperationalError as e:
                print(f"[DB] FTS5 may not be available: {e}")

    def _load_bloom_filter(self):
        with self._get_conn() as conn:
            rows = conn.execute(
                'SELECT infohash FROM infohashes'
            ).fetchall()
            for row in rows:
                self.bloom_filter.add(row['infohash'])

            rows = conn.execute(
                'SELECT infohash, expires_at FROM blacklist WHERE expires_at > ?',
                (int(time.time()),)
            ).fetchall()
            for row in rows:
                self._blacklist_cache[row['infohash']] = row['expires_at']

            print(f"[DB] Bloom filter loaded with {self.bloom_filter.estimated_count()} items")
            print(f"[DB] Blacklist loaded with {len(self._blacklist_cache)} items")

    def is_blacklisted(self, infohash: str) -> bool:
        now = int(time.time())
        if infohash in self._blacklist_cache:
            if self._blacklist_cache[infohash] > now:
                return True
            else:
                del self._blacklist_cache[infohash]
        return False

    def add_to_blacklist(self, infohash: str, reason: str = "invalid") -> None:
        now = int(time.time())
        expires = now + self.BLACKLIST_TTL_SECONDS
        self._blacklist_cache[infohash] = expires

        with self._get_conn() as conn:
            conn.execute('''
                INSERT OR REPLACE INTO blacklist (infohash, reason, added_at, expires_at)
                VALUES (?, ?, ?, ?)
            ''', (infohash, reason, now, expires))

        self.bloom_filter.add(infohash)
        print(f"[DB] Blacklisted: {infohash} (reason: {reason}, expires: {expires})")

    def cleanup_expired_blacklist(self) -> int:
        now = int(time.time())
        with self._get_conn() as conn:
            conn.execute('DELETE FROM blacklist WHERE expires_at <= ?', (now,))

        expired = [h for h, exp in self._blacklist_cache.items() if exp <= now]
        for h in expired:
            del self._blacklist_cache[h]
        return len(expired)

    def has_been_tried(self, infohash: str) -> bool:
        return infohash in self.bloom_filter

    def increment_retry(self, infohash: str) -> int:
        with self._get_conn() as conn:
            conn.execute('''
                UPDATE infohashes SET retry_count = retry_count + 1,
                last_attempt = ? WHERE infohash = ?
            ''', (int(time.time()), infohash))
            row = conn.execute(
                'SELECT retry_count FROM infohashes WHERE infohash = ?',
                (infohash,)
            ).fetchone()
            count = row['retry_count'] if row else 0
            self._retry_count[infohash] = count
            return count

    def get_retry_count(self, infohash: str) -> int:
        if infohash in self._retry_count:
            return self._retry_count[infohash]
        with self._get_conn() as conn:
            row = conn.execute(
                'SELECT retry_count FROM infohashes WHERE infohash = ?',
                (infohash,)
            ).fetchone()
            return row['retry_count'] if row else 0

    def should_retry(self, infohash: str) -> bool:
        return self.get_retry_count(infohash) < self.MAX_RETRY

    def record_success(self, infohash: str) -> None:
        with self._stats_lock:
            self._failure_stats['success'] += 1

    def record_failure(self, infohash: str) -> None:
        with self._stats_lock:
            self._failure_stats['failure'] += 1

    def get_failure_rate(self) -> float:
        with self._stats_lock:
            total = self._failure_stats['success'] + self._failure_stats['failure']
            if total == 0:
                return 0.0
            return self._failure_stats['failure'] / total

    def reset_failure_stats(self) -> None:
        with self._stats_lock:
            self._failure_stats = {'success': 0, 'failure': 0}

    def insert_infohash(self, infohash: str, source_ip: str,
                        source_port: int, timestamp: int) -> bool:
        if self.is_blacklisted(infohash):
            print(f"[DB] Skipping blacklisted infohash: {infohash}")
            return False

        with self._get_conn() as conn:
            try:
                conn.execute('''
                    INSERT OR IGNORE INTO infohashes
                    (infohash, source_ip, source_port, first_seen, last_seen)
                    VALUES (?, ?, ?, ?, ?)
                ''', (infohash, source_ip, source_port, timestamp, timestamp))

                conn.execute('''
                    UPDATE infohashes SET last_seen = ? WHERE infohash = ?
                ''', (timestamp, infohash))

                conn.execute('''
                    INSERT INTO download_history (infohash, timestamp)
                    VALUES (?, ?)
                ''', (infohash, timestamp))

                self.bloom_filter.add(infohash)
                return True
            except sqlite3.Error as e:
                print(f"[DB] insert_infohash error: {e}")
                return False

    def update_download_count(self, infohash: str) -> None:
        with self._get_conn() as conn:
            conn.execute('''
                UPDATE infohashes SET download_count = download_count + 1
                WHERE infohash = ?
            ''', (infohash,))

    def update_estimated_seeders(self, infohash: str, seeders: int) -> None:
        with self._get_conn() as conn:
            conn.execute('''
                UPDATE infohashes SET estimated_seeders = ? WHERE infohash = ?
            ''', (seeders, infohash))

    def upsert_torrent_meta(self, infohash: str, name: str, total_size: int,
                            piece_length: int, piece_count: int,
                            piece_hashes: list, files: list) -> bool:
        with self._get_conn() as conn:
            try:
                conn.execute('''
                    INSERT OR REPLACE INTO torrents
                    (infohash, name, total_size, piece_length, piece_count,
                     piece_hashes, files, parsed_at)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                ''', (infohash, name, total_size, piece_length, piece_count,
                      json.dumps(piece_hashes), json.dumps(files), int(time.time())))

                conn.execute('''
                    UPDATE infohashes SET status = 'parsed', retry_count = 0
                    WHERE infohash = ?
                ''', (infohash,))

                try:
                    conn.execute('''
                        INSERT INTO torrent_fts (rowid, name, files, infohash)
                        VALUES (last_insert_rowid(), ?, ?, ?)
                    ''', (name, ' '.join(files), infohash))
                except sqlite3.OperationalError:
                    pass

                return True
            except sqlite3.Error as e:
                print(f"[DB] upsert_torrent_meta error: {e}")
                return False

    def search_torrents(self, query: str, limit: int = 50) -> List[Dict]:
        with self._get_conn() as conn:
            try:
                rows = conn.execute('''
                    SELECT t.*, i.download_count, i.seeders, i.leechers,
                           i.estimated_seeders, i.first_seen, i.last_seen
                    FROM torrent_fts fts
                    JOIN torrents t ON t.rowid = fts.rowid
                    JOIN infohashes i ON i.infohash = t.infohash
                    WHERE torrent_fts MATCH ?
                    ORDER BY rank
                    LIMIT ?
                ''', (query, limit)).fetchall()
                return [dict(row) for row in rows]
            except sqlite3.OperationalError:
                rows = conn.execute('''
                    SELECT t.*, i.download_count, i.seeders, i.leechers,
                           i.estimated_seeders, i.first_seen, i.last_seen
                    FROM torrents t
                    JOIN infohashes i ON i.infohash = t.infohash
                    WHERE t.name LIKE ? OR t.files LIKE ?
                    ORDER BY i.download_count DESC
                    LIMIT ?
                ''', (f'%{query}%', f'%{query}%', limit)).fetchall()
                return [dict(row) for row in rows]

    def get_hot_resources(self, hours: int = 24, limit: int = 100) -> List[Dict]:
        cutoff = int(time.time()) - (hours * 3600)
        with self._get_conn() as conn:
            rows = conn.execute('''
                SELECT t.*, i.download_count, i.seeders, i.leechers,
                       i.estimated_seeders, i.first_seen, i.last_seen,
                       (SELECT COUNT(*) FROM download_history dh
                        WHERE dh.infohash = i.infohash AND dh.timestamp >= ?
                       ) as recent_downloads
                FROM infohashes i
                LEFT JOIN torrents t ON t.infohash = i.infohash
                ORDER BY recent_downloads DESC
                LIMIT ?
            ''', (cutoff, limit)).fetchall()
            return [dict(row) for row in rows]

    def get_resources(self, page: int = 1, per_page: int = 20,
                      status: str = None) -> Dict[str, Any]:
        offset = (page - 1) * per_page
        with self._get_conn() as conn:
            if status:
                count = conn.execute(
                    'SELECT COUNT(*) FROM infohashes WHERE status = ?',
                    (status,)
                ).fetchone()[0]
                rows = conn.execute('''
                    SELECT i.*, t.name, t.total_size, t.files, t.piece_count
                    FROM infohashes i
                    LEFT JOIN torrents t ON t.infohash = i.infohash
                    WHERE i.status = ?
                    ORDER BY i.last_seen DESC
                    LIMIT ? OFFSET ?
                ''', (status, per_page, offset)).fetchall()
            else:
                count = conn.execute('SELECT COUNT(*) FROM infohashes').fetchone()[0]
                rows = conn.execute('''
                    SELECT i.*, t.name, t.total_size, t.files, t.piece_count
                    FROM infohashes i
                    LEFT JOIN torrents t ON t.infohash = i.infohash
                    ORDER BY i.last_seen DESC
                    LIMIT ? OFFSET ?
                ''', (per_page, offset)).fetchall()

            return {
                'total': count,
                'page': page,
                'per_page': per_page,
                'items': [dict(row) for row in rows]
            }

    def get_pending_resources_smart(self, limit: int = 50) -> List[Dict]:
        with self._get_conn() as conn:
            rows = conn.execute('''
                SELECT i.*, t.name, t.total_size, t.files, t.piece_count
                FROM infohashes i
                LEFT JOIN torrents t ON t.infohash = i.infohash
                WHERE i.status = 'pending'
                  AND i.retry_count < ?
                ORDER BY i.estimated_seeders DESC, i.download_count DESC, i.last_seen DESC
                LIMIT ?
            ''', (self.MAX_RETRY, limit)).fetchall()
            return [dict(row) for row in rows]

    def get_resource_detail(self, infohash: str) -> Optional[Dict]:
        with self._get_conn() as conn:
            row = conn.execute('''
                SELECT i.*, t.name, t.total_size, t.piece_length, t.piece_count,
                       t.piece_hashes, t.files, t.parsed_at
                FROM infohashes i
                LEFT JOIN torrents t ON t.infohash = i.infohash
                WHERE i.infohash = ?
            ''', (infohash,)).fetchone()
            return dict(row) if row else None

    def infohash_exists(self, infohash: str) -> bool:
        with self._get_conn() as conn:
            row = conn.execute(
                'SELECT 1 FROM infohashes WHERE infohash = ?',
                (infohash,)
            ).fetchone()
            return row is not None

    def update_health(self, infohash: str, seeders: int, leechers: int) -> None:
        with self._get_conn() as conn:
            conn.execute('''
                UPDATE infohashes SET seeders = ?, leechers = ?
                WHERE infohash = ?
            ''', (seeders, leechers, infohash))

    def mark_invalid(self, infohash: str, reason: str = "metadata_timeout") -> None:
        retry = self.increment_retry(infohash)
        if retry >= self.MAX_RETRY:
            self.add_to_blacklist(infohash, reason)
            with self._get_conn() as conn:
                conn.execute(
                    "UPDATE infohashes SET status = 'invalid' WHERE infohash = ?",
                    (infohash,)
                )

    def get_stats(self) -> Dict[str, Any]:
        with self._get_conn() as conn:
            total = conn.execute('SELECT COUNT(*) FROM infohashes').fetchone()[0]
            parsed = conn.execute(
                "SELECT COUNT(*) FROM infohashes WHERE status = 'parsed'"
            ).fetchone()[0]
            pending = conn.execute(
                "SELECT COUNT(*) FROM infohashes WHERE status = 'pending'"
            ).fetchone()[0]
            invalid = conn.execute(
                "SELECT COUNT(*) FROM infohashes WHERE status = 'invalid'"
            ).fetchone()[0]
            total_size = conn.execute(
                'SELECT COALESCE(SUM(total_size), 0) FROM torrents'
            ).fetchone()[0]
            blacklist_count = conn.execute(
                'SELECT COUNT(*) FROM blacklist WHERE expires_at > ?',
                (int(time.time()),)
            ).fetchone()[0]

            return {
                'total_infohashes': total,
                'parsed_count': parsed,
                'pending_count': pending,
                'invalid_count': invalid,
                'blacklist_count': blacklist_count,
                'total_size_bytes': total_size,
                'total_size_readable': self._human_readable_size(total_size),
                'failure_rate': round(self.get_failure_rate(), 4),
                'metadata_timeout': self.METADATA_TIMEOUT,
                'max_retry': self.MAX_RETRY,
                'blacklist_ttl_hours': self.BLACKLIST_TTL_SECONDS // 3600
            }

    @staticmethod
    def _human_readable_size(size_bytes: int) -> str:
        if size_bytes == 0:
            return '0 B'
        units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']
        idx = 0
        while size_bytes >= 1024 and idx < len(units) - 1:
            size_bytes /= 1024
            idx += 1
        return f'{size_bytes:.2f} {units[idx]}'

    def import_fingerprint_db(self, fingerprint_db_path: str) -> int:
        import json
        if not os.path.exists(fingerprint_db_path):
            print(f"[DB] Fingerprint DB not found: {fingerprint_db_path}")
            return 0

        try:
            with open(fingerprint_db_path, 'r', encoding='utf-8') as f:
                data = json.load(f)

            fingerprints = data.get('fingerprints', data) if isinstance(data, dict) else data
            count = 0

            with self._get_conn() as conn:
                for fp in fingerprints:
                    try:
                        conn.execute('''
                            INSERT OR REPLACE INTO copyright_fingerprints
                            (md5, sha1, sha256, phash, file_size, filename,
                             title, copyright_holder, original_source, added_at,
                             fingerprint_version)
                            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                        ''', (
                            fp.get('md5', ''),
                            fp.get('sha1', ''),
                            fp.get('sha256', ''),
                            fp.get('phash', ''),
                            fp.get('size', fp.get('file_size', 0)),
                            fp.get('filename', ''),
                            fp.get('title', fp.get('filename', '')),
                            fp.get('copyright_holder', 'Unknown'),
                            fp.get('original_source', ''),
                            int(time.time()),
                            fp.get('fingerprint_version', '2.0')
                        ))
                        count += 1
                    except sqlite3.Error as e:
                        continue

            print(f"[DB] Imported {count} copyright fingerprints from {fingerprint_db_path}")
            return count
        except Exception as e:
            print(f"[DB] Failed to import fingerprint DB: {e}")
            return 0

    def get_all_fingerprints(self) -> List[Dict]:
        with self._get_conn() as conn:
            rows = conn.execute(
                'SELECT * FROM copyright_fingerprints ORDER BY copyright_holder'
            ).fetchall()
            return [dict(row) for row in rows]

    def get_fingerprint_count(self) -> int:
        with self._get_conn() as conn:
            return conn.execute(
                'SELECT COUNT(*) FROM copyright_fingerprints'
            ).fetchone()[0]

    def find_fingerprint_by_phash(self, phash: str, threshold: float = 0.95) -> Optional[Dict]:
        with self._get_conn() as conn:
            rows = conn.execute(
                'SELECT * FROM copyright_fingerprints'
            ).fetchall()
            for row in rows:
                row_dict = dict(row)
                sample_phash = row_dict.get('phash', '')
                if not sample_phash:
                    continue
                distance = sum(c1 != c2 for c1, c2 in zip(phash, sample_phash))
                max_len = max(len(phash), len(sample_phash))
                similarity = 1.0 - (distance / max_len) if max_len > 0 else 0
                if similarity >= threshold:
                    row_dict['similarity'] = similarity
                    return row_dict
        return None

    def record_infringement(self, infohash: str, file_name: str, file_size: int,
                            phash: str, matched_fp: Dict, similarity: float,
                            uploader_ip: str = None, source_ip: str = None) -> int:
        evidence = {
            'infohash': infohash,
            'file_name': file_name,
            'file_size': file_size,
            'phash': phash,
            'matched_fingerprint': matched_fp,
            'similarity': similarity,
            'uploader_ip': uploader_ip,
            'source_ip': source_ip,
            'detected_at': int(time.time())
        }

        with self._get_conn() as conn:
            cursor = conn.execute('''
                INSERT INTO infringement_records
                (infohash, file_name, file_size, phash, matched_fingerprint_id,
                 matched_title, copyright_holder, similarity, uploader_ip,
                 source_ip, detected_at, evidence_json)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ''', (
                infohash, file_name, file_size, phash,
                matched_fp.get('id'),
                matched_fp.get('title', matched_fp.get('filename', '')),
                matched_fp.get('copyright_holder', 'Unknown'),
                similarity, uploader_ip, source_ip,
                int(time.time()),
                json.dumps(evidence, ensure_ascii=False)
            ))
            return cursor.lastrowid

    def get_infringement_records(self, status: str = None,
                                 copyright_holder: str = None,
                                 limit: int = 100) -> List[Dict]:
        with self._get_conn() as conn:
            sql = 'SELECT * FROM infringement_records WHERE 1=1'
            params = []
            if status:
                sql += ' AND status = ?'
                params.append(status)
            if copyright_holder:
                sql += ' AND copyright_holder = ?'
                params.append(copyright_holder)
            sql += ' ORDER BY detected_at DESC LIMIT ?'
            params.append(limit)
            rows = conn.execute(sql, params).fetchall()
            return [dict(row) for row in rows]

    def get_infringement_stats_by_holder(self) -> List[Dict]:
        with self._get_conn() as conn:
            rows = conn.execute('''
                SELECT copyright_holder,
                       COUNT(*) as total_infringements,
                       COUNT(DISTINCT infohash) as unique_resources,
                       SUM(CASE WHEN dmca_generated = 1 THEN 1 ELSE 0 END) as dmca_sent,
                       SUM(CASE WHEN status = 'confirmed' THEN 1 ELSE 0 END) as confirmed,
                       MAX(detected_at) as last_detected
                FROM infringement_records
                GROUP BY copyright_holder
                ORDER BY total_infringements DESC
            ''').fetchall()
            return [dict(row) for row in rows]

    def is_infringing(self, infohash: str) -> bool:
        with self._get_conn() as conn:
            row = conn.execute(
                'SELECT 1 FROM infringement_records WHERE infohash = ? AND status != "false_positive" LIMIT 1',
                (infohash,)
            ).fetchone()
            return row is not None

    def mark_infringement_status(self, record_id: int, status: str) -> None:
        with self._get_conn() as conn:
            conn.execute(
                'UPDATE infringement_records SET status = ? WHERE id = ?',
                (status, record_id)
            )

    def save_dmca_notice(self, infringement_id: int, infohash: str,
                         recipient_email: str, subject: str,
                         notice_text: str) -> int:
        with self._get_conn() as conn:
            cursor = conn.execute('''
                INSERT INTO dmca_notices
                (infringement_id, infohash, recipient_email, subject,
                 notice_text, generated_at, status)
                VALUES (?, ?, ?, ?, ?, ?, 'generated')
            ''', (infringement_id, infohash, recipient_email, subject,
                  notice_text, int(time.time())))

            conn.execute(
                'UPDATE infringement_records SET dmca_generated = 1 WHERE id = ?',
                (infringement_id,)
            )
            return cursor.lastrowid

    def get_dmca_notices(self, infringement_id: int = None,
                         limit: int = 50) -> List[Dict]:
        with self._get_conn() as conn:
            if infringement_id:
                rows = conn.execute('''
                    SELECT * FROM dmca_notices WHERE infringement_id = ?
                    ORDER BY generated_at DESC LIMIT ?
                ''', (infringement_id, limit)).fetchall()
            else:
                rows = conn.execute('''
                    SELECT * FROM dmca_notices ORDER BY generated_at DESC LIMIT ?
                ''', (limit,)).fetchall()
            return [dict(row) for row in rows]

    def get_copyright_stats(self) -> Dict[str, Any]:
        with self._get_conn() as conn:
            fingerprint_count = conn.execute(
                'SELECT COUNT(*) FROM copyright_fingerprints'
            ).fetchone()[0]
            infringement_total = conn.execute(
                'SELECT COUNT(*) FROM infringement_records'
            ).fetchone()[0]
            infringement_confirmed = conn.execute(
                "SELECT COUNT(*) FROM infringement_records WHERE status = 'confirmed'"
            ).fetchone()[0]
            infringement_suspected = conn.execute(
                "SELECT COUNT(*) FROM infringement_records WHERE status = 'suspected'"
            ).fetchone()[0]
            dmca_total = conn.execute(
                'SELECT COUNT(*) FROM dmca_notices'
            ).fetchone()[0]
            dmca_sent = conn.execute(
                "SELECT COUNT(*) FROM dmca_notices WHERE status = 'sent'"
            ).fetchone()[0]
            holders = conn.execute('''
                SELECT COUNT(DISTINCT copyright_holder) FROM copyright_fingerprints
            ''').fetchone()[0]

            return {
                'fingerprint_count': fingerprint_count,
                'copyright_holders': holders,
                'infringement_total': infringement_total,
                'infringement_confirmed': infringement_confirmed,
                'infringement_suspected': infringement_suspected,
                'dmca_total': dmca_total,
                'dmca_sent': dmca_sent,
                'similarity_threshold': self.COPYRIGHT_THRESHOLD
            }
