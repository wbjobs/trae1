import os
import numpy as np
import cv2
from typing import Optional, List
from loguru import logger
from config import deep_feature_config


class DeepFeatureExtractor:
    def __init__(self,
                 model_path: str = deep_feature_config.model_path,
                 feature_dim: int = deep_feature_config.feature_dim,
                 input_size: tuple = (224, 224),
                 mean: tuple = deep_feature_config.input_mean,
                 std: tuple = deep_feature_config.input_std,
                 use_cuda: bool = deep_feature_config.use_cuda,
                 intra_op_num_threads: int = deep_feature_config.intra_op_num_threads):
        self.model_path = model_path
        self.feature_dim = feature_dim
        self.input_size = input_size
        self.mean = np.array(mean, dtype=np.float32).reshape(3, 1, 1)
        self.std = np.array(std, dtype=np.float32).reshape(3, 1, 1)
        self.use_cuda = use_cuda
        self.intra_op_num_threads = intra_op_num_threads
        self.session = None
        self.input_name = None
        self.output_name = None
        self._init_session()

    def _init_session(self):
        try:
            import onnxruntime as ort
            
            if not os.path.exists(self.model_path):
                logger.warning(f"模型文件不存在: {self.model_path}")
                logger.info("请先运行 python export_model.py 导出ONNX模型")
                raise FileNotFoundError(f"ONNX模型未找到: {self.model_path}")

            providers = ['CUDAExecutionProvider', 'CPUExecutionProvider'] if self.use_cuda else ['CPUExecutionProvider']
            
            sess_options = ort.SessionOptions()
            sess_options.intra_op_num_threads = self.intra_op_num_threads
            sess_options.log_severity_level = 3
            
            self.session = ort.InferenceSession(
                self.model_path,
                sess_options=sess_options,
                providers=providers
            )
            
            self.input_name = self.session.get_inputs()[0].name
            self.output_name = self.session.get_outputs()[0].name
            
            provider = self.session.get_providers()[0]
            logger.info(f"ONNX Runtime 已初始化, 使用: {provider}")
            logger.info(f"输入: {self.input_name}, 输出: {self.output_name}")
            
        except ImportError:
            logger.error("onnxruntime 未安装, 请运行: pip install onnxruntime")
            raise
        except Exception as e:
            logger.error(f"初始化ONNX Runtime失败: {e}")
            raise

    def _preprocess(self, frame: np.ndarray) -> np.ndarray:
        frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        
        resized = cv2.resize(frame_rgb, self.input_size, interpolation=cv2.INTER_AREA)
        
        tensor = resized.transpose(2, 0, 1).astype(np.float32) / 255.0
        
        normalized = (tensor - self.mean) / self.std
        
        return normalized.reshape(1, 3, *self.input_size)

    def extract(self, frame: np.ndarray) -> np.ndarray:
        if self.session is None:
            raise RuntimeError("ONNX模型未初始化")
        
        preprocessed = self._preprocess(frame)
        
        features = self.session.run(
            [self.output_name],
            {self.input_name: preprocessed}
        )[0]
        
        features = features.flatten()
        
        norm = np.linalg.norm(features)
        if norm > 0:
            features = features / norm
        
        return features.astype(np.float32)

    def extract_batch(self, frames: List[np.ndarray]) -> np.ndarray:
        if not frames:
            return np.array([], dtype=np.float32).reshape(0, self.feature_dim)
        
        preprocessed_batch = np.vstack([self._preprocess(f) for f in frames])
        
        features = self.session.run(
            [self.output_name],
            {self.input_name: preprocessed_batch}
        )[0]
        
        norms = np.linalg.norm(features, axis=1, keepdims=True)
        norms = np.where(norms > 0, norms, 1)
        features = features / norms
        
        return features.astype(np.float32)

    def benchmark(self, num_iter: int = 100) -> float:
        dummy_frame = np.random.randint(0, 255, (640, 480, 3), dtype=np.uint8)
        
        self.extract(dummy_frame)
        
        import time
        start = time.time()
        
        for _ in range(num_iter):
            self.extract(dummy_frame)
        
        elapsed = time.time() - start
        avg_ms = (elapsed / num_iter) * 1000
        
        logger.info(f"深度特征提取平均耗时: {avg_ms:.2f} ms/帧")
        return avg_ms

    def close(self):
        if self.session is not None:
            self.session = None
            logger.info("ONNX Runtime 已释放")
