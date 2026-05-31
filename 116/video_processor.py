import cv2
import subprocess
import numpy as np
from typing import Iterator, Tuple, Optional
from loguru import logger
from config import video_config


class FrameExtractor:
    def __init__(self, target_width: int = video_config.target_width,
                 target_height: int = video_config.target_height,
                 frame_interval: float = video_config.frame_interval):
        self.target_width = target_width
        self.target_height = target_height
        self.frame_interval = frame_interval

    def _is_stream_url(self, source: str) -> bool:
        return source.startswith(('rtmp://', 'rtsp://', 'http://', 'https://', 'hls://', 'm3u8'))

    def _get_stream_info(self, source: str) -> Tuple[float, int, int]:
        probe = subprocess.run(
            ['ffprobe', '-v', 'quiet', '-print_format', 'json',
             '-show_streams', '-select_streams', 'v:0', source],
            capture_output=True, text=True
        )
        import json
        info = json.loads(probe.stdout)
        streams = info.get('streams', [])
        if not streams:
            raise RuntimeError(f"无法获取视频流信息: {source}")
        
        stream = streams[0]
        fps_str = stream.get('r_frame_rate', '30/1')
        num, den = map(int, fps_str.split('/'))
        fps = num / den if den else 30.0
        width = int(stream.get('width', 0))
        height = int(stream.get('height', 0))
        return fps, width, height

    def extract_frames(self, source: str, max_frames: Optional[int] = None) -> Iterator[Tuple[np.ndarray, float]]:
        is_stream = self._is_stream_url(source)
        
        try:
            fps, width, height = self._get_stream_info(source)
            logger.info(f"视频源: {source}, FPS: {fps:.2f}, 分辨率: {width}x{height}")
        except Exception as e:
            logger.warning(f"ffprobe获取信息失败，使用OpenCV: {e}")
            fps = 30.0
        
        if is_stream:
            yield from self._extract_from_stream(source, fps, max_frames)
        else:
            yield from self._extract_from_file(source, fps, max_frames)

    def _extract_from_stream(self, source: str, fps: float, max_frames: Optional[int] = None) -> Iterator[Tuple[np.ndarray, float]]:
        ffmpeg_cmd = [
            'ffmpeg',
            '-i', source,
            '-vf', f'fps={1/self.frame_interval},scale={self.target_width}:{self.target_height}',
            '-pix_fmt', 'bgr24',
            '-f', 'rawvideo',
            '-hide_banner', '-loglevel', 'error',
            'pipe:1'
        ]
        
        process = subprocess.Popen(
            ffmpeg_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=10**8
        )
        
        frame_size = self.target_width * self.target_height * 3
        frame_count = 0
        timestamp = 0.0
        
        try:
            while True:
                raw_frame = process.stdout.read(frame_size)
                if len(raw_frame) != frame_size:
                    break
                
                frame = np.frombuffer(raw_frame, dtype=np.uint8)
                frame = frame.reshape((self.target_height, self.target_width, 3))
                
                yield frame, timestamp
                timestamp += self.frame_interval
                frame_count += 1
                
                if max_frames and frame_count >= max_frames:
                    break
        finally:
            process.stdout.close()
            process.stderr.close()
            process.wait()

    def _extract_from_file(self, source: str, fps: float, max_frames: Optional[int] = None) -> Iterator[Tuple[np.ndarray, float]]:
        cap = cv2.VideoCapture(source)
        if not cap.isOpened():
            raise RuntimeError(f"无法打开视频文件: {source}")
        
        frame_interval_frames = max(1, int(fps * self.frame_interval))
        frame_count = 0
        extracted_count = 0
        
        try:
            while True:
                ret, frame = cap.read()
                if not ret:
                    break
                
                if frame_count % frame_interval_frames == 0:
                    timestamp = frame_count / fps
                    frame = cv2.resize(frame, (self.target_width, self.target_height))
                    yield frame, timestamp
                    extracted_count += 1
                    
                    if max_frames and extracted_count >= max_frames:
                        break
                
                frame_count += 1
        finally:
            cap.release()


class VideoProcessor:
    def __init__(self):
        self.frame_extractor = FrameExtractor()

    def get_video_duration(self, source: str) -> float:
        try:
            probe = subprocess.run(
                ['ffprobe', '-v', 'quiet', '-print_format', 'json',
                 '-show_entries', 'format=duration', source],
                capture_output=True, text=True
            )
            import json
            info = json.loads(probe.stdout)
            duration = float(info['format']['duration'])
            return duration
        except Exception as e:
            logger.warning(f"获取视频时长失败: {e}")
            return 0.0
