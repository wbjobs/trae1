import os
import io
import hashlib
import struct
from typing import List, Tuple, Optional, Dict, Any
import threading

try:
    from PIL import Image
    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False

try:
    import numpy as np
    NUMPY_AVAILABLE = True
except ImportError:
    NUMPY_AVAILABLE = False


class PerceptualHash:
    def __init__(self, hash_size: int = 8, freq_size: int = 32):
        self.hash_size = hash_size
        self.freq_size = freq_size
        self._lock = threading.RLock()

    def dct_2d(self, matrix: List[List[float]]) -> List[List[float]]:
        if not NUMPY_AVAILABLE:
            return self._dct_2d_simple(matrix)
        return self._dct_2d_numpy(matrix)

    def _dct_2d_simple(self, matrix: List[List[float]]) -> List[List[float]]:
        n = len(matrix)
        result = [[0.0] * n for _ in range(n)]
        for u in range(n):
            for v in range(n):
                cu = 1.0 if u == 0 else (2.0 ** 0.5) / 2.0
                cv = 1.0 if v == 0 else (2.0 ** 0.5) / 2.0
                total = 0.0
                for x in range(n):
                    for y in range(n):
                        total += matrix[x][y] * \
                                 math.cos((2 * x + 1) * u * math.pi / (2 * n)) * \
                                 math.cos((2 * y + 1) * v * math.pi / (2 * n))
                result[u][v] = cu * cv * total / 2.0
        return result

    def _dct_2d_numpy(self, matrix: List[List[float]]) -> List[List[float]]:
        import numpy as np
        arr = np.array(matrix, dtype=np.float64)
        from scipy.fftpack import dct
        dct_result = dct(dct(arr.T, norm='ortho').T, norm='ortho')
        return dct_result.tolist()

    def compute_phash(self, image_data: bytes) -> Optional[str]:
        if not PIL_AVAILABLE:
            return self._compute_phash_fallback(image_data)

        try:
            with self._lock:
                img = Image.open(io.BytesIO(image_data))
                img = img.convert('L').resize(
                    (self.freq_size, self.freq_size),
                    Image.Resampling.LANCZOS if hasattr(Image, 'Resampling') else Image.LANCZOS
                )

                pixels = list(img.getdata())
                matrix = [
                    pixels[i:i + self.freq_size]
                    for i in range(0, len(pixels), self.freq_size)
                ]

                if NUMPY_AVAILABLE:
                    try:
                        from scipy.fftpack import dct
                        arr = np.array(matrix, dtype=np.float64)
                        dct_result = dct(dct(arr.T, norm='ortho').T, norm='ortho')
                        low_freq = dct_result[:self.hash_size, :self.hash_size]
                    except Exception:
                        low_freq = [row[:self.hash_size] for row in matrix[:self.hash_size]]
                else:
                    low_freq = [row[:self.hash_size] for row in matrix[:self.hash_size]]

                flat = [item for row in low_freq for item in row]
                avg = sum(flat) / len(flat)

                bits = []
                for val in flat:
                    bits.append('1' if val > avg else '0')

                return ''.join(bits)

        except Exception as e:
            return self._compute_phash_fallback(image_data)

    def _compute_phash_fallback(self, data: bytes) -> str:
        chunk_size = 1024
        chunks = []
        for i in range(0, len(data), chunk_size):
            chunk = data[i:i + chunk_size]
            if len(chunk) < chunk_size:
                chunk = chunk + b'\x00' * (chunk_size - len(chunk))
            chunks.append(chunk)

        if not chunks:
            chunks = [b'\x00' * chunk_size]

        feature_matrix = []
        for chunk in chunks[:64]:
            row = []
            for i in range(0, len(chunk), 8):
                val = sum(chunk[i:i + 8]) / 8.0 if i + 8 <= len(chunk) else 0
                row.append(val)
            feature_matrix.append(row)

        flat = [item for row in feature_matrix for item in row]
        if not flat:
            return '0' * 64

        avg = sum(flat) / len(flat)
        bits = []
        for val in flat[:64]:
            bits.append('1' if val > avg else '0')

        return ''.join(bits)

    def compute_file_phash(self, file_path: str,
                           file_size: Optional[int] = None) -> Optional[str]:
        try:
            with open(file_path, 'rb') as f:
                if file_size and file_size > 5 * 1024 * 1024:
                    sample_size = 1024 * 1024
                    samples = []
                    f.seek(0)
                    samples.append(f.read(sample_size // 3))
                    if file_size > sample_size:
                        f.seek(file_size // 2)
                        samples.append(f.read(sample_size // 3))
                        f.seek(file_size - sample_size // 3)
                        samples.append(f.read(sample_size // 3))
                    data = b''.join(samples)
                else:
                    data = f.read(5 * 1024 * 1024)
            return self.compute_phash(data)
        except Exception as e:
            return None

    def compute_bytes_phash(self, data: bytes) -> str:
        return self.compute_phash(data) or self._compute_phash_fallback(data)

    @staticmethod
    def hamming_distance(hash1: str, hash2: str) -> int:
        if len(hash1) != len(hash2):
            return abs(len(hash1) - len(hash2)) + 64
        return sum(c1 != c2 for c1, c2 in zip(hash1, hash2))

    @staticmethod
    def similarity(hash1: str, hash2: str) -> float:
        if not hash1 or not hash2:
            return 0.0
        max_len = max(len(hash1), len(hash2))
        if max_len == 0:
            return 0.0
        distance = PerceptualHash.hamming_distance(hash1, hash2)
        return 1.0 - (distance / max_len)

    @staticmethod
    def is_similar(hash1: str, hash2: str, threshold: float = 0.95) -> bool:
        return PerceptualHash.similarity(hash1, hash2) >= threshold


class ContentFingerprint:
    def __init__(self, phash_size: int = 8):
        self.phash = PerceptualHash(phash_size)
        self.md5_cache: Dict[str, str] = {}
        self._cache_lock = threading.Lock()

    def generate_fingerprint(self, data: bytes, filename: str = None) -> Dict[str, Any]:
        md5 = hashlib.md5(data).hexdigest()
        sha1 = hashlib.sha1(data).hexdigest()
        sha256 = hashlib.sha256(data).hexdigest()

        perceptual_hash = self.phash.compute_bytes_phash(data)

        return {
            'md5': md5,
            'sha1': sha1,
            'sha256': sha256,
            'phash': perceptual_hash,
            'size': len(data),
            'filename': filename,
            'fingerprint_version': '2.0'
        }

    def generate_file_fingerprint(self, file_path: str) -> Optional[Dict[str, Any]]:
        try:
            with open(file_path, 'rb') as f:
                data = f.read(5 * 1024 * 1024)

            md5 = hashlib.md5()
            sha1 = hashlib.sha1()
            sha256 = hashlib.sha256()

            with open(file_path, 'rb') as f:
                while chunk := f.read(8192):
                    md5.update(chunk)
                    sha1.update(chunk)
                    sha256.update(chunk)

            with self._cache_lock:
                self.md5_cache[file_path] = md5.hexdigest()

            perceptual_hash = self.phash.compute_file_phash(file_path)

            return {
                'md5': md5.hexdigest(),
                'sha1': sha1.hexdigest(),
                'sha256': sha256.hexdigest(),
                'phash': perceptual_hash,
                'size': os.path.getsize(file_path),
                'filename': os.path.basename(file_path),
                'path': file_path,
                'fingerprint_version': '2.0'
            }
        except Exception as e:
            return None

    def match_fingerprint(self, target_fingerprint: Dict[str, Any],
                          sample_fingerprints: List[Dict[str, Any]],
                          threshold: float = 0.95) -> List[Tuple[Dict[str, Any], float]]:
        matches = []
        target_phash = target_fingerprint.get('phash', '')
        target_md5 = target_fingerprint.get('md5', '')

        for sample in sample_fingerprints:
            if sample.get('md5') == target_md5:
                matches.append((sample, 1.0))
                continue

            sample_phash = sample.get('phash', '')
            similarity = PerceptualHash.similarity(target_phash, sample_phash)
            if similarity >= threshold:
                matches.append((sample, similarity))

        matches.sort(key=lambda x: x[1], reverse=True)
        return matches


class CopyrightDatabase:
    def __init__(self, db_path: str = None):
        self.db_path = db_path
        self.fingerprints: List[Dict[str, Any]] = []
        self.by_copyright_holder: Dict[str, List[Dict[str, Any]]] = {}
        self._lock = threading.RLock()

    def load(self, db_path: str = None) -> int:
        path = db_path or self.db_path
        if not path or not os.path.exists(path):
            return 0

        try:
            with open(path, 'r', encoding='utf-8') as f:
                import json
                data = json.load(f)

            with self._lock:
                if isinstance(data, list):
                    self.fingerprints = data
                elif isinstance(data, dict) and 'fingerprints' in data:
                    self.fingerprints = data['fingerprints']

                self.by_copyright_holder.clear()
                for fp in self.fingerprints:
                    holder = fp.get('copyright_holder', 'Unknown')
                    if holder not in self.by_copyright_holder:
                        self.by_copyright_holder[holder] = []
                    self.by_copyright_holder[holder].append(fp)

            return len(self.fingerprints)
        except Exception as e:
            return 0

    def save(self, db_path: str = None) -> bool:
        path = db_path or self.db_path
        if not path:
            return False

        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'w', encoding='utf-8') as f:
                import json
                json.dump({
                    'version': '2.0',
                    'generated_at': int(time.time()),
                    'fingerprint_count': len(self.fingerprints),
                    'fingerprints': self.fingerprints
                }, f, indent=2, ensure_ascii=False)
            return True
        except Exception as e:
            return False

    def add_fingerprint(self, fingerprint: Dict[str, Any],
                        copyright_holder: str = 'Unknown',
                        title: str = None) -> bool:
        try:
            entry = dict(fingerprint)
            entry['copyright_holder'] = copyright_holder
            entry['title'] = title or entry.get('filename', 'Unknown')
            entry['added_at'] = int(time.time())

            with self._lock:
                self.fingerprints.append(entry)
                if copyright_holder not in self.by_copyright_holder:
                    self.by_copyright_holder[copyright_holder] = []
                self.by_copyright_holder[copyright_holder].append(entry)

            return True
        except Exception:
            return False

    def find_matches(self, target_fingerprint: Dict[str, Any],
                     threshold: float = 0.95) -> List[Tuple[Dict[str, Any], float]]:
        with self._lock:
            fps = list(self.fingerprints)

        matcher = ContentFingerprint()
        return matcher.match_fingerprint(target_fingerprint, fps, threshold)

    def get_by_holder(self, holder: str) -> List[Dict[str, Any]]:
        with self._lock:
            return list(self.by_copyright_holder.get(holder, []))

    def get_holders(self) -> List[str]:
        with self._lock:
            return list(self.by_copyright_holder.keys())

    def count(self) -> int:
        with self._lock:
            return len(self.fingerprints)


class DMCAGenerator:
    TEMPLATE = """From: {sender_name} <{sender_email}>
To: {recipient_email}
Subject: DMCA Takedown Notice - {work_title}

Date: {date}

DMCA TAKEDOWN NOTICE

I, {sender_name}, am the copyright owner or an agent authorized to act on behalf
of the copyright owner of the copyrighted work identified below.

1. IDENTIFICATION OF THE COPYRIGHTED WORK:
   - Work Title: {work_title}
   - Copyright Holder: {copyright_holder}
   - Original Source: {original_source}

2. IDENTIFICATION OF INFRINGING MATERIAL:
   - Resource InfoHash: {infohash}
   - File Name(s): {file_names}
   - File Size: {file_size}
   - Detected Similarity: {similarity:.1%}
   - Perceptual Hash: {phash}

3. YOUR INFORMATION:
   - Uploader IP (if available): {uploader_ip}
   - Detection Timestamp: {detection_time}

4. STATEMENT:
I have a good faith belief that the use of the material in the manner complained
of is not authorized by the copyright owner, its agent, or the law.

I swear, under penalty of perjury, that the information in this notification is
accurate and that I am the copyright owner or am authorized to act on behalf of
the owner of an exclusive right that is allegedly infringed.

5. CONTACT INFORMATION:
   Name: {sender_name}
   Email: {sender_email}
   Phone: {sender_phone}
   Address: {sender_address}

Please remove or disable access to the infringing material within 48 hours.

Sincerely,
{sender_name}
{signature}
"""

    @staticmethod
    def generate_notice(infringement: Dict[str, Any],
                        sender_info: Dict[str, str],
                        work_info: Dict[str, str]) -> str:
        import datetime
        now = datetime.datetime.now()

        return DMCAGenerator.TEMPLATE.format(
            sender_name=sender_info.get('name', 'Copyright Agent'),
            sender_email=sender_info.get('email', 'dmca@example.com'),
            sender_phone=sender_info.get('phone', '+1-XXX-XXX-XXXX'),
            sender_address=sender_info.get('address', 'Address'),
            recipient_email=work_info.get('recipient_email', 'abuse@example.com'),
            work_title=work_info.get('title', 'Unknown Work'),
            copyright_holder=work_info.get('copyright_holder', 'Unknown'),
            original_source=work_info.get('original_source', 'Unknown'),
            infohash=infringement.get('infohash', 'Unknown'),
            file_names=infringement.get('file_names', 'Unknown'),
            file_size=infringement.get('file_size_readable', 'Unknown'),
            similarity=infringement.get('similarity', 0.0),
            phash=infringement.get('phash', 'Unknown'),
            uploader_ip=infringement.get('uploader_ip', 'Not available'),
            detection_time=datetime.datetime.fromtimestamp(
                infringement.get('detected_at', int(time.time()))
            ).isoformat(),
            date=now.strftime('%Y-%m-%d %H:%M:%S'),
            signature='-- ' + sender_info.get('name', 'Copyright Agent')
        )

    @staticmethod
    def generate_batch_notices(infringements: List[Dict[str, Any]],
                               sender_info: Dict[str, str],
                               group_by_holder: bool = True) -> Dict[str, List[str]]:
        notices: Dict[str, List[str]] = {}

        for inf in infringements:
            holder = inf.get('copyright_holder', 'Unknown')
            work_info = {
                'title': inf.get('matched_title', 'Unknown Work'),
                'copyright_holder': holder,
                'original_source': inf.get('matched_source', 'Unknown'),
                'recipient_email': inf.get('recipient_email', 'abuse@example.com')
            }
            notice = DMCAGenerator.generate_notice(inf, sender_info, work_info)

            if group_by_holder:
                if holder not in notices:
                    notices[holder] = []
                notices[holder].append(notice)
            else:
                if 'all' not in notices:
                    notices['all'] = []
                notices['all'].append(notice)

        return notices


import time
import math
