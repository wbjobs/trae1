import subprocess
import numpy as np
from typing import List, Optional, Tuple, Iterator
from loguru import logger
from config import audio_config


class AudioFingerprintExtractor:
    def __init__(self,
                 sample_rate: int = audio_config.sample_rate,
                 n_mfcc: int = audio_config.n_mfcc,
                 hop_length: int = audio_config.hop_length,
                 audio_interval: float = audio_config.audio_interval,
                 fingerprint_dim: int = audio_config.fingerprint_dim):
        self.sample_rate = sample_rate
        self.n_mfcc = n_mfcc
        self.hop_length = hop_length
        self.audio_interval = audio_interval
        self.fingerprint_dim = fingerprint_dim
        self.librosa_available = self._check_librosa()

    def _check_librosa(self) -> bool:
        try:
            import librosa
            return True
        except ImportError:
            logger.warning("librosa 未安装，将使用FFMPEG + numpy 进行基础音频特征提取")
            return False

    def _extract_audio(self, video_source: str) -> Optional[np.ndarray]:
        ffmpeg_cmd = [
            'ffmpeg',
            '-i', video_source,
            '-vn',
            '-acodec', 'pcm_s16le',
            '-ar', str(self.sample_rate),
            '-ac', '1',
            '-f', 's16le',
            '-hide_banner', '-loglevel', 'error',
            'pipe:1'
        ]

        try:
            process = subprocess.run(
                ffmpeg_cmd,
                capture_output=True,
                check=True
            )
            
            audio_data = np.frombuffer(process.stdout, dtype=np.int16)
            audio_data = audio_data.astype(np.float32) / 32768.0
            
            logger.info(f"提取音频成功, 时长: {len(audio_data)/self.sample_rate:.2f}秒")
            return audio_data
        except subprocess.CalledProcessError as e:
            logger.warning(f"提取音频失败 (可能无音轨): {e}")
            return None
        except Exception as e:
            logger.error(f"音频提取错误: {e}")
            return None

    def _compute_mfcc_librosa(self, audio: np.ndarray) -> np.ndarray:
        import librosa
        
        mfcc = librosa.feature.mfcc(
            y=audio,
            sr=self.sample_rate,
            n_mfcc=self.n_mfcc,
            hop_length=self.hop_length
        )
        
        mfcc_delta = librosa.feature.delta(mfcc)
        mfcc_delta2 = librosa.feature.delta(mfcc, order=2)
        
        features = np.vstack([mfcc, mfcc_delta, mfcc_delta2])
        
        return features

    def _compute_mfcc_numpy(self, audio: np.ndarray) -> np.ndarray:
        n_fft = 2048
        n_mels = self.n_mfcc
        
        frames = []
        for i in range(0, len(audio) - n_fft, self.hop_length):
            frame = audio[i:i + n_fft] * np.hanning(n_fft)
            spectrum = np.abs(np.fft.rfft(frame)) ** 2
            mel_filter = self._create_mel_filter(n_fft, n_mels)
            mel_spectrum = np.dot(mel_filter, spectrum)
            log_mel = np.log(mel_spectrum + 1e-10)
            frames.append(log_mel)
        
        if not frames:
            return np.zeros((self.n_mfcc * 3, 1))
        
        features = np.array(frames).T
        
        features_d1 = np.diff(features, axis=1, prepend=features[:, 0:1])
        features_d2 = np.diff(features_d1, axis=1, prepend=features_d1[:, 0:1])
        
        return np.vstack([features, features_d1, features_d2])

    def _create_mel_filter(self, n_fft: int, n_mels: int) -> np.ndarray:
        n_freqs = n_fft // 2 + 1
        
        mel_low = 0
        mel_high = 2595 * np.log10(1 + self.sample_rate / 2 / 700)
        mel_points = np.linspace(mel_low, mel_high, n_mels + 2)
        hz_points = 700 * (10 ** (mel_points / 2595) - 1)
        
        bin_points = np.floor((n_fft + 1) * hz_points / self.sample_rate).astype(int)
        
        filter_bank = np.zeros((n_mels, n_freqs))
        
        for m in range(1, n_mels + 1):
            f_m_minus = bin_points[m - 1]
            f_m = bin_points[m]
            f_m_plus = bin_points[m + 1]
            
            for k in range(f_m_minus, f_m):
                filter_bank[m - 1, k] = (k - f_m_minus) / (f_m - f_m_minus)
            for k in range(f_m, f_m_plus):
                filter_bank[m - 1, k] = (f_m_plus - k) / (f_m_plus - f_m)
        
        return filter_bank

    def _compute_fingerprint(self, features: np.ndarray) -> np.ndarray:
        if features.size == 0:
            return np.zeros(self.fingerprint_dim, dtype=np.float32)
        
        mean_feat = np.mean(features, axis=1)
        std_feat = np.std(features, axis=1)
        max_feat = np.max(features, axis=1)
        min_feat = np.min(features, axis=1)
        
        combined = np.concatenate([mean_feat, std_feat, max_feat, min_feat])
        
        if len(combined) > self.fingerprint_dim:
            combined = combined[:self.fingerprint_dim]
        elif len(combined) < self.fingerprint_dim:
            combined = np.pad(combined, (0, self.fingerprint_dim - len(combined)), mode='constant')
        
        norm = np.linalg.norm(combined)
        if norm > 0:
            combined = combined / norm
        
        return combined.astype(np.float32)

    def extract(self, video_source: str) -> List[Tuple[np.ndarray, float]]:
        audio_data = self._extract_audio(video_source)
        if audio_data is None:
            return []
        
        if self.librosa_available:
            features = self._compute_mfcc_librosa(audio_data)
        else:
            features = self._compute_mfcc_numpy(audio_data)
        
        samples_per_interval = int(self.sample_rate * self.audio_interval)
        frames_per_interval = max(1, samples_per_interval // self.hop_length)
        
        fingerprints = []
        total_frames = features.shape[1]
        
        for i in range(0, total_frames, frames_per_interval):
            end_idx = min(i + frames_per_interval, total_frames)
            segment_features = features[:, i:end_idx]
            fp = self._compute_fingerprint(segment_features)
            timestamp = (i * self.hop_length) / self.sample_rate
            fingerprints.append((fp, timestamp))
        
        logger.info(f"提取音频指纹 {len(fingerprints)} 个")
        return fingerprints

    def extract_segment(self, audio_segment: np.ndarray) -> np.ndarray:
        if self.librosa_available:
            features = self._compute_mfcc_librosa(audio_segment)
        else:
            features = self._compute_mfcc_numpy(audio_segment)
        
        return self._compute_fingerprint(features)

    def extract_stream(self, video_source: str) -> Iterator[Tuple[np.ndarray, float]]:
        audio_data = self._extract_audio(video_source)
        if audio_data is None:
            return
        
        if self.librosa_available:
            features = self._compute_mfcc_librosa(audio_data)
        else:
            features = self._compute_mfcc_numpy(audio_data)
        
        frames_per_interval = max(1, int(self.sample_rate * self.audio_interval) // self.hop_length)
        total_frames = features.shape[1]
        
        for i in range(0, total_frames, frames_per_interval):
            end_idx = min(i + frames_per_interval, total_frames)
            segment_features = features[:, i:end_idx]
            fp = self._compute_fingerprint(segment_features)
            timestamp = (i * self.hop_length) / self.sample_rate
            yield fp, timestamp

    def similarity(self, fp1: np.ndarray, fp2: np.ndarray) -> float:
        if np.linalg.norm(fp1) == 0 or np.linalg.norm(fp2) == 0:
            return 0.0
        
        cosine_sim = np.dot(fp1, fp2) / (np.linalg.norm(fp1) * np.linalg.norm(fp2))
        return max(0.0, min(1.0, (cosine_sim + 1.0) / 2.0))

    def get_audio_fingerprint_at(self, fingerprints: List[Tuple[np.ndarray, float]], 
                                  timestamp: float) -> Optional[np.ndarray]:
        if not fingerprints:
            return None
        
        best_fp = None
        min_diff = float('inf')
        
        for fp, ts in fingerprints:
            diff = abs(ts - timestamp)
            if diff < min_diff:
                min_diff = diff
                best_fp = fp
        
        if min_diff <= self.audio_interval * 2:
            return best_fp
        return None
