import cv2
import numpy as np
from PIL import Image
import imagehash
from typing import Tuple, List, Optional, Dict, Any
from dataclasses import dataclass
from loguru import logger
from config import video_config, fusion_config
from skimage.feature import local_binary_pattern


@dataclass
class MultimodalFingerprint:
    fingerprint: bytes
    feature: Optional[np.ndarray] = None
    audio: Optional[np.ndarray] = None
    timestamp: float = 0.0
    frame_index: int = 0

    def to_dict(self) -> Dict[str, Any]:
        return {
            "fingerprint": self.fingerprint.hex(),
            "feature": self.feature.tolist() if self.feature is not None else None,
            "audio": self.audio.tolist() if self.audio is not None else None,
            "timestamp": self.timestamp,
            "frame_index": self.frame_index
        }


class FingerprintGenerator:
    def __init__(self, fingerprint_bytes: int = video_config.fingerprint_bytes):
        self.fingerprint_bytes = fingerprint_bytes
        self.phash_size = 8
        self.color_bins = 16
        self.fp_weight = fusion_config.fingerprint_weight
        self.feat_weight = fusion_config.feature_weight
        self.audio_weight = fusion_config.audio_weight
        self.enable_audio = fusion_config.enable_audio

    def generate(self, frame: np.ndarray) -> bytes:
        phash_bits = self._compute_phash(frame)
        color_bits = self._compute_color_histogram(frame)
        lbp_bits = self._compute_lbp(frame)
        
        combined_bits = phash_bits + color_bits + lbp_bits
        fingerprint = self._bits_to_bytes(combined_bits)
        
        return fingerprint[:self.fingerprint_bytes]

    def generate_multimodal(self, frame: np.ndarray,
                           deep_feature: Optional[np.ndarray] = None,
                           audio_fingerprint: Optional[np.ndarray] = None,
                           timestamp: float = 0.0,
                           frame_index: int = 0) -> MultimodalFingerprint:
        fingerprint = self.generate(frame)
        
        return MultimodalFingerprint(
            fingerprint=fingerprint,
            feature=deep_feature,
            audio=audio_fingerprint,
            timestamp=timestamp,
            frame_index=frame_index
        )

    def fused_similarity(self,
                        fp1: MultimodalFingerprint,
                        fp2: MultimodalFingerprint) -> float:
        scores = []
        weights = []
        
        fp_sim = self.similarity(fp1.fingerprint, fp2.fingerprint)
        scores.append(fp_sim)
        weights.append(self.fp_weight)
        
        if fp1.feature is not None and fp2.feature is not None:
            feat_sim = self._cosine_similarity(fp1.feature, fp2.feature)
            scores.append(feat_sim)
            weights.append(self.feat_weight)
        
        if (self.enable_audio and 
            fp1.audio is not None and 
            fp2.audio is not None):
            audio_sim = self._cosine_similarity(fp1.audio, fp2.audio)
            scores.append(audio_sim)
            weights.append(self.audio_weight)
        
        total_weight = sum(weights)
        if total_weight == 0:
            return 0.0
        
        weights = [w / total_weight for w in weights]
        fused_score = sum(s * w for s, w in zip(scores, weights))
        
        return max(0.0, min(1.0, fused_score))

    def _cosine_similarity(self, v1: np.ndarray, v2: np.ndarray) -> float:
        if np.linalg.norm(v1) == 0 or np.linalg.norm(v2) == 0:
            return 0.0
        sim = np.dot(v1, v2) / (np.linalg.norm(v1) * np.linalg.norm(v2))
        return max(0.0, min(1.0, (sim + 1.0) / 2.0))

    def _compute_phash(self, frame: np.ndarray) -> List[int]:
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        pil_image = Image.fromarray(gray)
        phash = imagehash.phash(pil_image, hash_size=self.phash_size)
        return self._hex_to_bits(str(phash))

    def _compute_color_histogram(self, frame: np.ndarray) -> List[int]:
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        
        hist_h = cv2.calcHist([hsv], [0], None, [self.color_bins], [0, 180])
        hist_s = cv2.calcHist([hsv], [1], None, [self.color_bins // 2], [0, 256])
        hist_v = cv2.calcHist([hsv], [2], None, [self.color_bins // 2], [0, 256])
        
        hist_h = cv2.normalize(hist_h, hist_h).flatten()
        hist_s = cv2.normalize(hist_s, hist_s).flatten()
        hist_v = cv2.normalize(hist_v, hist_v).flatten()
        
        bits = []
        threshold_h = np.mean(hist_h)
        threshold_s = np.mean(hist_s)
        threshold_v = np.mean(hist_v)
        
        for val in hist_h:
            bits.append(1 if val > threshold_h else 0)
        for val in hist_s:
            bits.append(1 if val > threshold_s else 0)
        for val in hist_v:
            bits.append(1 if val > threshold_v else 0)
        
        return bits[:32]

    def _compute_lbp(self, frame: np.ndarray) -> List[int]:
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        
        lbp = local_binary_pattern(gray, P=8, R=1, method='uniform')
        n_bins = int(lbp.max() + 1)
        hist, _ = np.histogram(lbp, density=True, bins=n_bins, range=(0, n_bins))
        
        hist = cv2.normalize(hist, hist).flatten()
        
        bits = []
        threshold = np.mean(hist)
        for val in hist[:32]:
            bits.append(1 if val > threshold else 0)
        
        while len(bits) < 32:
            bits.append(0)
        
        return bits[:32]

    def _hex_to_bits(self, hex_str: str) -> List[int]:
        bits = []
        for char in hex_str:
            val = int(char, 16)
            for i in range(4):
                bits.append((val >> (3 - i)) & 1)
        return bits

    def _bits_to_bytes(self, bits: List[int]) -> bytes:
        byte_list = []
        for i in range(0, len(bits), 8):
            byte = 0
            for j in range(8):
                if i + j < len(bits):
                    byte = (byte << 1) | bits[i + j]
                else:
                    byte = byte << 1
            byte_list.append(byte)
        return bytes(byte_list)

    def fingerprint_to_bits(self, fingerprint: bytes) -> List[int]:
        bits = []
        for byte in fingerprint:
            for i in range(8):
                bits.append((byte >> (7 - i)) & 1)
        return bits

    def hamming_distance(self, fp1: bytes, fp2: bytes) -> int:
        distance = 0
        for b1, b2 in zip(fp1, fp2):
            distance += bin(b1 ^ b2).count('1')
        return distance

    def similarity(self, fp1: bytes, fp2: bytes) -> float:
        max_distance = len(fp1) * 8
        distance = self.hamming_distance(fp1, fp2)
        return 1.0 - (distance / max_distance)

    def batch_generate(self, frames: List[np.ndarray]) -> List[bytes]:
        return [self.generate(frame) for frame in frames]


class FingerprintSequence:
    def __init__(self, video_id: str):
        self.video_id = video_id
        self.fingerprints: List[bytes] = []
        self.features: List[Optional[np.ndarray]] = []
        self.audios: List[Optional[np.ndarray]] = []
        self.timestamps: List[float] = []
        self.multimodal_fps: List[MultimodalFingerprint] = []

    def add(self, fingerprint: bytes, timestamp: float,
            feature: Optional[np.ndarray] = None,
            audio: Optional[np.ndarray] = None):
        self.fingerprints.append(fingerprint)
        self.features.append(feature)
        self.audios.append(audio)
        self.timestamps.append(timestamp)
        self.multimodal_fps.append(MultimodalFingerprint(
            fingerprint=fingerprint,
            feature=feature,
            audio=audio,
            timestamp=timestamp,
            frame_index=len(self.fingerprints) - 1
        ))

    def add_multimodal(self, multimodal_fp: MultimodalFingerprint):
        self.multimodal_fps.append(multimodal_fp)
        self.fingerprints.append(multimodal_fp.fingerprint)
        self.features.append(multimodal_fp.feature)
        self.audios.append(multimodal_fp.audio)
        self.timestamps.append(multimodal_fp.timestamp)

    def __len__(self) -> int:
        return len(self.fingerprints)

    def get_sequence(self) -> Tuple[List[bytes], List[float]]:
        return self.fingerprints, self.timestamps

    def get_multimodal_sequence(self) -> List[MultimodalFingerprint]:
        return self.multimodal_fps

    def get_features(self) -> np.ndarray:
        valid_features = [f for f in self.features if f is not None]
        if not valid_features:
            return np.array([], dtype=np.float32)
        return np.vstack(valid_features)

    def export(self, include_frames: bool = False) -> Dict[str, Any]:
        export_data = {
            "video_id": self.video_id,
            "timestamps": self.timestamps,
            "fingerprints": [fp.hex() for fp in self.fingerprints],
            "features": [f.tolist() if f is not None else None for f in self.features],
            "audios": [a.tolist() if a is not None else None for a in self.audios],
            "frame_count": len(self)
        }
        return export_data
