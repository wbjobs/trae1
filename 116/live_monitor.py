import cv2
import numpy as np
import time
import threading
from datetime import datetime
from typing import List, Dict, Optional, Callable
from loguru import logger
from dataclasses import dataclass, field
from config import LiveStreamConfig, video_config
from fingerprint_generator import FingerprintGenerator, MultimodalFingerprint
from matcher import FingerprintMatcher


@dataclass
class StreamMonitoringResult:
    stream_name: str
    stream_url: str
    station: str
    scanned_duration: float
    fps_extracted: int
    matched_videos: List[Dict] = field(default_factory=list)
    scan_time: datetime = field(default_factory=datetime.now)
    
    def to_dict(self) -> Dict:
        return {
            "stream_name": self.stream_name,
            "stream_url": self.stream_url,
            "station": self.station,
            "scanned_duration": self.scanned_duration,
            "fps_extracted": self.fps_extracted,
            "matched_videos": self.matched_videos,
            "scan_time": self.scan_time.isoformat()
        }


class LiveStreamMonitor:
    def __init__(self, fp_generator: FingerprintGenerator, matcher: FingerprintMatcher,
                 deep_feature_extractor=None, audio_extractor=None):
        self.streams: List[LiveStreamConfig] = []
        self.fp_generator = fp_generator
        self.matcher = matcher
        self.deep_feature_extractor = deep_feature_extractor
        self.audio_extractor = audio_extractor
        self._monitoring_threads: Dict[str, threading.Thread] = {}
        self._stop_events: Dict[str, threading.Event] = {}
        self._scan_results: Dict[str, StreamMonitoringResult] = {}
        self._infringement_callbacks: List[Callable] = []
        self.max_frames_per_scan = 120

    def add_stream(self, config: LiveStreamConfig) -> None:
        self.streams.append(config)
        logger.info(f"已添加直播流监控: {config.name} ({config.url})")

    def remove_stream(self, name: str) -> bool:
        for i, stream in enumerate(self.streams):
            if stream.name == name:
                self.streams.pop(i)
                self.stop_monitoring_stream(name)
                logger.info(f"已移除直播流监控: {name}")
                return True
        return False

    def get_streams(self) -> List[Dict]:
        return [{
            "name": stream.name,
            "url": stream.url,
            "station": stream.station,
            "enabled": stream.enabled,
            "check_interval": stream.check_interval,
            "scan_duration": stream.scan_duration,
            "is_monitoring": stream.name in self._monitoring_threads
        } for stream in self.streams]

    def add_infringement_callback(self, callback: Callable) -> None:
        self._infringement_callbacks.append(callback)

    def extract_frames_from_stream(self, stream_url: str, duration: int) -> List[MultimodalFingerprint]:
        fingerprints = []
        cap = None
        
        try:
            logger.info(f"开始从直播流提取帧: {stream_url}, 持续时间: {duration}秒")
            
            cap = cv2.VideoCapture(stream_url)
            if not cap.isOpened():
                logger.error(f"无法打开直播流: {stream_url}")
                return []

            fps = cap.get(cv2.CAP_PROP_FPS) or 25
            frame_interval = int(fps * video_config.frame_interval)
            if frame_interval < 1:
                frame_interval = 1

            start_time = time.time()
            frame_count = 0
            extracted_count = 0

            while (time.time() - start_time) < duration and extracted_count < self.max_frames_per_scan:
                ret, frame = cap.read()
                if not ret:
                    logger.warning(f"读取帧失败，可能流已中断")
                    time.sleep(0.5)
                    continue

                if frame_count % frame_interval == 0:
                    try:
                        timestamp = extracted_count * video_config.frame_interval
                        
                        fp = self.fp_generator.generate(frame)
                        
                        feature = None
                        if self.deep_feature_extractor:
                            feature = self.deep_feature_extractor.extract(frame)
                        
                        multimodal_fp = MultimodalFingerprint(
                            fingerprint=fp,
                            feature=feature,
                            audio=None,
                            timestamp=timestamp,
                            frame_index=extracted_count
                        )
                        
                        fingerprints.append(multimodal_fp)
                        extracted_count += 1
                        
                    except Exception as e:
                        logger.error(f"处理帧失败: {e}")

                frame_count += 1

            logger.info(f"从直播流提取了 {extracted_count} 帧指纹")

        except Exception as e:
            logger.error(f"从直播流提取帧失败: {e}")
        finally:
            if cap:
                cap.release()

        return fingerprints

    def scan_stream(self, stream_config: LiveStreamConfig) -> Optional[StreamMonitoringResult]:
        if not stream_config.enabled:
            return None

        logger.info(f"开始扫描直播流: {stream_config.name}")
        
        fingerprints = self.extract_frames_from_stream(
            stream_config.url, 
            stream_config.scan_duration
        )

        if not fingerprints:
            logger.warning(f"从直播流未提取到指纹: {stream_config.name}")
            return None

        result = StreamMonitoringResult(
            stream_name=stream_config.name,
            stream_url=stream_config.url,
            station=stream_config.station,
            scanned_duration=stream_config.scan_duration,
            fps_extracted=len(fingerprints)
        )

        try:
            if self.deep_feature_extractor:
                matches = self.matcher.match_multimodal(fingerprints, top_k=10)
            else:
                pure_fps = [fp.fingerprint for fp in fingerprints]
                matches = self.matcher.match_sequence(pure_fps)

            result.matched_videos = matches
            
            for match in matches:
                logger.info(f"在直播流 {stream_config.name} 中发现匹配视频: "
                           f"{match['video_id']}, 相似度: {match['confidence']:.4f}")
                
                for callback in self._infringement_callbacks:
                    try:
                        callback({
                            "type": "live_stream",
                            "stream_name": stream_config.name,
                            "stream_url": stream_config.url,
                            "station": stream_config.station,
                            "match": match
                        })
                    except Exception as e:
                        logger.error(f"执行回调失败: {e}")

        except Exception as e:
            logger.error(f"匹配直播流指纹失败: {e}")

        self._scan_results[stream_config.name] = result
        return result

    def scan_all_streams(self) -> List[StreamMonitoringResult]:
        results = []
        for stream in self.streams:
            if stream.enabled:
                result = self.scan_stream(stream)
                if result:
                    results.append(result)
        return results

    def start_monitoring_stream(self, stream_name: str) -> bool:
        if stream_name in self._monitoring_threads:
            logger.warning(f"直播流 {stream_name} 已经在监控中")
            return False

        stream_config = None
        for stream in self.streams:
            if stream.name == stream_name:
                stream_config = stream
                break

        if not stream_config:
            logger.error(f"未找到直播流配置: {stream_name}")
            return False

        stop_event = threading.Event()
        self._stop_events[stream_name] = stop_event

        def monitor_loop():
            logger.info(f"开始持续监控直播流: {stream_name}")
            while not stop_event.is_set():
                try:
                    self.scan_stream(stream_config)
                except Exception as e:
                    logger.error(f"监控直播流时出错: {e}")
                
                for _ in range(stream_config.check_interval):
                    if stop_event.is_set():
                        break
                    time.sleep(1)
            
            logger.info(f"直播流监控已停止: {stream_name}")

        thread = threading.Thread(target=monitor_loop, daemon=True)
        self._monitoring_threads[stream_name] = thread
        thread.start()

        logger.info(f"直播流监控线程已启动: {stream_name}")
        return True

    def stop_monitoring_stream(self, stream_name: str) -> bool:
        if stream_name not in self._monitoring_threads:
            return False

        stop_event = self._stop_events.get(stream_name)
        if stop_event:
            stop_event.set()

        thread = self._monitoring_threads.get(stream_name)
        if thread and thread.is_alive():
            thread.join(timeout=5)

        if stream_name in self._monitoring_threads:
            del self._monitoring_threads[stream_name]
        if stream_name in self._stop_events:
            del self._stop_events[stream_name]

        logger.info(f"已停止直播流监控: {stream_name}")
        return True

    def start_all_monitoring(self) -> None:
        for stream in self.streams:
            if stream.enabled:
                self.start_monitoring_stream(stream.name)

    def stop_all_monitoring(self) -> None:
        for stream_name in list(self._monitoring_threads.keys()):
            self.stop_monitoring_stream(stream_name)

    def get_scan_results(self) -> Dict[str, Dict]:
        return {name: result.to_dict() for name, result in self._scan_results.items()}

    def capture_screenshot(self, stream_url: str, output_path: str) -> bool:
        cap = None
        try:
            cap = cv2.VideoCapture(stream_url)
            if not cap.isOpened():
                return False

            ret, frame = cap.read()
            if ret:
                cv2.imwrite(output_path, frame)
                return True

        except Exception as e:
            logger.error(f"截图失败: {e}")
        finally:
            if cap:
                cap.release()

        return False
