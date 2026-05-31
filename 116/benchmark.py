import time
import numpy as np
from typing import List, Tuple
from loguru import logger
from config import video_config, fusion_config
from fingerprint_generator import FingerprintGenerator, MultimodalFingerprint
from milvus_store import MilvusStore
from matcher import FingerprintMatcher
from deep_feature_extractor import DeepFeatureExtractor
from audio_fingerprint import AudioFingerprintExtractor
from export_model import export_mobilenetv3_onnx
import os


def generate_mock_fingerprints(count: int, generator: FingerprintGenerator) -> Tuple[list, list]:
    logger.info(f"生成 {count} 条模拟指纹...")
    fingerprints = []
    timestamps = []
    
    for i in range(count):
        frame = np.random.randint(0, 255, (64, 64, 3), dtype=np.uint8)
        fp = generator.generate(frame)
        fingerprints.append(fp)
        timestamps.append(i * video_config.frame_interval)
    
    return fingerprints, timestamps


def generate_mock_multimodal(count: int, generator: FingerprintGenerator, 
                             feature_extractor: DeepFeatureExtractor) -> List[MultimodalFingerprint]:
    logger.info(f"生成 {count} 条多模态模拟指纹...")
    multimodal_fps = []
    
    for i in range(count):
        frame = np.random.randint(0, 255, (224, 224, 3), dtype=np.uint8)
        fp = generator.generate(frame)
        feature = feature_extractor.extract(frame)
        multimodal_fps.append(MultimodalFingerprint(
            fingerprint=fp,
            feature=feature,
            audio=np.random.randn(128).astype(np.float32),
            timestamp=i * video_config.frame_interval,
            frame_index=i
        ))
    
    return multimodal_fps


def benchmark_deep_feature_extraction(extractor: DeepFeatureExtractor, num_frames: int = 100):
    logger.info(f"开始深度特征提取性能测试，{num_frames} 帧...")
    frames = [np.random.randint(0, 255, (224, 224, 3), dtype=np.uint8) for _ in range(num_frames)]
    
    for _ in range(5):
        extractor.extract(frames[0])
    
    times = []
    for frame in frames:
        start = time.perf_counter()
        feature = extractor.extract(frame)
        elapsed = time.perf_counter() - start
        times.append(elapsed)
    
    avg_time = np.mean(times) * 1000
    min_time = np.min(times) * 1000
    max_time = np.max(times) * 1000
    fps = 1000 / avg_time
    
    logger.info(f"深度特征提取完成: {num_frames} 帧")
    logger.info(f"单帧推理: 平均 {avg_time:.2f}ms, 最小 {min_time:.2f}ms, 最大 {max_time:.2f}ms")
    logger.info(f"速度: {fps:.2f} 帧/秒 {'✅ 满足<30ms要求' if avg_time < 30 else '⚠️ 不满足<30ms要求'}")
    
    return avg_time, fps


def benchmark_fusion_similarity(generator: FingerprintGenerator, num_pairs: int = 1000):
    logger.info(f"开始多模态融合相似度计算性能测试，{num_pairs} 对...")
    fps = []
    
    for _ in range(num_pairs):
        frame1 = np.random.randint(0, 255, (64, 64, 3), dtype=np.uint8)
        frame2 = np.random.randint(0, 255, (64, 64, 3), dtype=np.uint8)
        fp1 = MultimodalFingerprint(
            fingerprint=generator.generate(frame1),
            feature=np.random.randn(256).astype(np.float32),
            audio=np.random.randn(128).astype(np.float32),
            timestamp=0
        )
        fp2 = MultimodalFingerprint(
            fingerprint=generator.generate(frame2),
            feature=np.random.randn(256).astype(np.float32),
            audio=np.random.randn(128).astype(np.float32),
            timestamp=1
        )
        fps.append((fp1, fp2))
    
    start_time = time.time()
    for fp1, fp2 in fps:
        sim = generator.fused_similarity(fp1, fp2)
    total_time = time.time() - start_time
    
    qps = num_pairs / total_time
    logger.info(f"融合相似度计算完成: {num_pairs} 对, 耗时: {total_time:.2f}秒, 速度: {qps:.2f} 次/秒")
    
    return total_time, qps


def benchmark_multimodal_insertion(store: MilvusStore, multimodal_fps: List[MultimodalFingerprint]):
    logger.info(f"开始多模态数据插入测试，{len(multimodal_fps)} 条...")
    start_time = time.time()
    
    video_id = f"benchmark_multimodal_{int(time.time())}"
    ids = store.insert_multimodal(video_id, multimodal_fps)
    
    total_time = time.time() - start_time
    fps = len(multimodal_fps) / total_time
    
    logger.info(f"多模态插入完成: {len(multimodal_fps)} 条, 耗时: {total_time:.2f}秒, 速度: {fps:.2f} 条/秒")
    return video_id, total_time, fps


def benchmark_multimodal_search(store: MilvusStore, num_queries: int = 100, top_k: int = 10):
    logger.info(f"开始多模态查询性能测试，{num_queries} 次...")
    queries = []
    
    for _ in range(num_queries):
        fp = os.urandom(16)
        feat = np.random.randn(256).astype(np.float32)
        audio = np.random.randn(128).astype(np.float32)
        queries.append((fp, feat, audio))
    
    times = []
    for fp, feat, audio in queries:
        start = time.time()
        store.search_multimodal(fp, feat, audio, top_k=top_k)
        elapsed = time.time() - start
        times.append(elapsed)
    
    avg_time = np.mean(times) * 1000
    qps = num_queries / np.sum(times)
    
    logger.info(f"多模态查询完成: {num_queries} 次, 平均 {avg_time:.2f}ms/次, 速度: {qps:.2f} QPS")
    return avg_time, qps


def benchmark_multimodal_matching(matcher: FingerprintMatcher, sequence_lengths: List[int] = [10, 20, 30]):
    for seq_len in sequence_lengths:
        logger.info(f"开始多模态序列匹配测试，序列长度: {seq_len}...")
        query_multimodal = []
        
        for _ in range(seq_len):
            fp = MultimodalFingerprint(
                fingerprint=os.urandom(16),
                feature=np.random.randn(256).astype(np.float32),
                audio=np.random.randn(128).astype(np.float32),
                timestamp=_ * video_config.frame_interval
            )
            query_multimodal.append(fp)
        
        start_time = time.time()
        results = matcher.match_multimodal(query_multimodal, top_k=10)
        total_time = time.time() - start_time
        
        logger.info(f"多模态序列匹配 {seq_len} 帧完成, 耗时: {total_time:.3f}秒, 结果数: {len(results)}")


def benchmark_audio_fingerprint(extractor: AudioFingerprintExtractor, num_samples: int = 100):
    logger.info(f"开始音频指纹提取性能测试，{num_samples} 条...")
    audio_datas = [np.random.randn(22050).astype(np.float32) for _ in range(num_samples)]
    
    start_time = time.time()
    for audio in audio_datas:
        if extractor.librosa_available:
            feat = extractor._compute_mfcc_librosa(audio)
        else:
            feat = extractor._compute_mfcc_numpy(audio)
    total_time = time.time() - start_time
    
    fps = num_samples / total_time
    logger.info(f"音频指纹提取完成: {num_samples} 条, 耗时: {total_time:.2f}秒, 速度: {fps:.2f} 条/秒")
    
    return fps


def benchmark_feature_export():
    logger.info("开始特征导出性能测试...")
    from main import export_features, FingerprintSequence
    
    sequence = FingerprintSequence("export_test")
    for i in range(100):
        sequence.add(
            fingerprint=os.urandom(16),
            timestamp=i * 1.0,
            feature=np.random.randn(256).astype(np.float32),
            audio=np.random.randn(128).astype(np.float32)
        )
    
    for fmt in ["numpy", "json"]:
        start_time = time.time()
        path = export_features("benchmark", sequence, format=fmt)
        total_time = time.time() - start_time
        file_size = os.path.getsize(path) / 1024
        
        logger.info(f"特征导出 ({fmt}): 耗时 {total_time:.3f}秒, 文件大小 {file_size:.1f} KB")
        os.unlink(path)


def ensure_model_exists():
    model_path = "models/mobilenetv3.onnx"
    if not os.path.exists(model_path):
        logger.warning(f"模型文件不存在: {model_path}")
        confirm = input("是否导出MobileNetV3 ONNX模型? (y/N): ").lower().strip()
        if confirm == 'y':
            os.makedirs("models", exist_ok=True)
            export_mobilenetv3_onnx(model_path, feature_dim=256)
            logger.info("模型导出成功")
            return True
        return False
    return True


def main():
    logger.info("=" * 80)
    logger.info("多模态视频指纹服务性能基准测试 v2.0")
    logger.info("=" * 80)
    
    logger.info(f"\n融合权重配置: pHash={fusion_config.fingerprint_weight:.2f}, "
                f"深度特征={fusion_config.feature_weight:.2f}, "
                f"音频={fusion_config.audio_weight:.2f}")
    
    feature_extractor = None
    audio_extractor = None
    
    model_exists = ensure_model_exists()
    if model_exists:
        try:
            feature_extractor = DeepFeatureExtractor()
            logger.info("✅ 深度特征提取器初始化成功")
        except Exception as e:
            logger.warning(f"深度特征提取器初始化失败: {e}")
    else:
        logger.warning("跳过深度特征相关测试")
    
    try:
        audio_extractor = AudioFingerprintExtractor()
        logger.info("✅ 音频指纹提取器初始化成功")
    except Exception as e:
        logger.warning(f"音频指纹提取器初始化失败: {e}")
    
    generator = FingerprintGenerator()
    logger.info("✅ 指纹生成器初始化成功")
    
    logger.info("\n1. 深度特征提取性能测试 (MobileNetV3 + ONNX Runtime)")
    logger.info("-" * 60)
    if feature_extractor:
        try:
            benchmark_deep_feature_extraction(feature_extractor, num_frames=100)
        except Exception as e:
            logger.error(f"深度特征测试失败: {e}")
    else:
        logger.info("跳过（模型不可用）")
    
    logger.info("\n2. 指纹生成性能测试 (pHash + 颜色直方图 + LBP)")
    logger.info("-" * 60)
    fps_gen, proc_time = benchmark_fingerprint_generation(generator, num_frames=500)
    
    if audio_extractor:
        logger.info("\n3. 音频指纹提取性能测试")
        logger.info("-" * 60)
        try:
            benchmark_audio_fingerprint(audio_extractor, num_samples=100)
        except Exception as e:
            logger.error(f"音频指纹测试失败: {e}")
    
    logger.info("\n4. 多模态融合相似度计算性能测试")
    logger.info("-" * 60)
    try:
        benchmark_fusion_similarity(generator, num_pairs=500)
    except Exception as e:
        logger.error(f"融合相似度测试失败: {e}")
    
    logger.info("\n5. 特征导出性能测试")
    logger.info("-" * 60)
    try:
        benchmark_feature_export()
    except Exception as e:
        logger.error(f"特征导出测试失败: {e}")
    
    try:
        store = MilvusStore()
        matcher = FingerprintMatcher(store, generator)
        logger.info("✅ Milvus存储初始化成功")
        
        logger.info("\n6. 单模态指纹插入性能测试")
        logger.info("-" * 60)
        mock_fps, mock_ts = generate_mock_fingerprints(5000, generator)
        video_id, _, _ = benchmark_insertion(store, mock_fps, mock_ts)
        
        if feature_extractor:
            logger.info("\n7. 多模态数据插入性能测试")
            logger.info("-" * 60)
            try:
                mock_multimodal = generate_mock_multimodal(2000, generator, feature_extractor)
                video_id_mm, _, _ = benchmark_multimodal_insertion(store, mock_multimodal)
            except Exception as e:
                logger.error(f"多模态插入测试失败: {e}")
        
        logger.info(f"\n当前数据库指纹总数: {store.count()}")
        
        logger.info("\n8. 单帧查询性能测试")
        logger.info("-" * 60)
        benchmark_search(store, generator, num_queries=100, top_k=10)
        
        if feature_extractor:
            logger.info("\n9. 多模态查询性能测试")
            logger.info("-" * 60)
            try:
                benchmark_multimodal_search(store, num_queries=50, top_k=10)
            except Exception as e:
                logger.error(f"多模态查询测试失败: {e}")
        
        logger.info("\n10. 批量查询性能测试")
        logger.info("-" * 60)
        benchmark_batch_search(store, generator, batch_sizes=[10, 30, 50])
        
        logger.info("\n11. 单模态序列匹配性能测试")
        logger.info("-" * 60)
        benchmark_sequence_matching(matcher, generator, sequence_lengths=[10, 20, 30])
        
        if feature_extractor:
            logger.info("\n12. 多模态序列匹配性能测试")
            logger.info("-" * 60)
            try:
                benchmark_multimodal_matching(matcher, sequence_lengths=[10, 20, 30])
            except Exception as e:
                logger.error(f"多模态序列匹配测试失败: {e}")
        
        logger.info("\n13. 大规模数据测试 (可选)")
        logger.info("-" * 60)
        confirm = input("是否进行十万级数据模拟测试? (y/N): ").lower().strip()
        if confirm == 'y':
            logger.info("生成10万条指纹用于测试...")
            for i in range(10):
                batch_fps, batch_ts = generate_mock_fingerprints(10000, generator)
                batch_video_id = f"scale_test_{i}_{int(time.time())}"
                store.insert_fingerprints(batch_video_id, batch_fps, batch_ts)
                logger.info(f"已插入 {(i+1)*10000} 条...")
            
            logger.info(f"数据库总数: {store.count()}")
            logger.info("进行大规模查询测试...")
            benchmark_search(store, generator, num_queries=50, top_k=100)
            if feature_extractor:
                benchmark_multimodal_search(store, num_queries=20, top_k=100)
        
        store.close()
        
    except Exception as e:
        logger.error(f"Milvus连接失败，跳过数据库相关测试: {e}")
        logger.info("请确保Milvus服务已启动并正确配置")
    
    logger.info("\n" + "=" * 80)
    logger.info("基准测试完成")
    logger.info("=" * 80)


if __name__ == "__main__":
    main()
