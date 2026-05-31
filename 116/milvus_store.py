import numpy as np
from typing import List, Dict, Optional, Tuple
from loguru import logger
from pymilvus import (
    connections,
    utility,
    FieldSchema,
    CollectionSchema,
    DataType,
    Collection,
)
from config import milvus_config, video_config
from fingerprint_generator import MultimodalFingerprint


class MilvusStore:
    def __init__(self, host: str = milvus_config.host,
                 port: int = milvus_config.port,
                 collection_name: str = milvus_config.collection_name,
                 fingerprint_dim: int = milvus_config.fingerprint_dim,
                 feature_dim: int = milvus_config.feature_dim,
                 audio_dim: int = milvus_config.audio_dim):
        self.host = host
        self.port = port
        self.collection_name = collection_name
        self.fingerprint_dim = fingerprint_dim
        self.feature_dim = feature_dim
        self.audio_dim = audio_dim
        self.collection: Optional[Collection] = None
        self._connect()
        self._init_collection()

    def _connect(self):
        try:
            connections.connect(
                alias="default",
                host=self.host,
                port=self.port
            )
            logger.info(f"已连接到Milvus: {self.host}:{self.port}")
        except Exception as e:
            logger.error(f"连接Milvus失败: {e}")
            raise

    def _init_collection(self):
        if utility.has_collection(self.collection_name):
            self.collection = Collection(self.collection_name)
            logger.info(f"加载已有集合: {self.collection_name}")
        else:
            self._create_collection()

        self._create_index()
        self.collection.load()

    def _create_collection(self):
        fields = [
            FieldSchema(name="id", dtype=DataType.INT64, is_primary=True, auto_id=True),
            FieldSchema(name="video_id", dtype=DataType.VARCHAR, max_length=128),
            FieldSchema(name="frame_index", dtype=DataType.INT64),
            FieldSchema(name="timestamp", dtype=DataType.FLOAT),
            FieldSchema(name="fingerprint", dtype=DataType.BINARY_VECTOR, dim=self.fingerprint_dim),
            FieldSchema(name="feature", dtype=DataType.FLOAT_VECTOR, dim=self.feature_dim),
            FieldSchema(name="audio", dtype=DataType.FLOAT_VECTOR, dim=self.audio_dim),
        ]
        
        schema = CollectionSchema(fields, description="多模态视频指纹库")
        self.collection = Collection(self.collection_name, schema)
        logger.info(f"创建集合: {self.collection_name}")

    def _create_index(self):
        try:
            fp_index_params = {
                "index_type": milvus_config.fingerprint_index_type,
                "metric_type": milvus_config.fingerprint_metric,
            }
            self.collection.create_index(
                field_name="fingerprint",
                index_params=fp_index_params
            )
            logger.info(f"指纹索引创建成功: {milvus_config.fingerprint_index_type}")
        except Exception as e:
            logger.warning(f"指纹索引可能已存在: {e}")

        try:
            feat_index_params = {
                "index_type": milvus_config.feature_index_type,
                "metric_type": milvus_config.feature_metric,
                "params": {"nlist": milvus_config.nlist}
            }
            self.collection.create_index(
                field_name="feature",
                index_params=feat_index_params
            )
            logger.info(f"深度特征索引创建成功: {milvus_config.feature_index_type}")
        except Exception as e:
            logger.warning(f"深度特征索引可能已存在: {e}")

        try:
            audio_index_params = {
                "index_type": milvus_config.feature_index_type,
                "metric_type": milvus_config.audio_metric,
                "params": {"nlist": milvus_config.nlist}
            }
            self.collection.create_index(
                field_name="audio",
                index_params=audio_index_params
            )
            logger.info(f"音频指纹索引创建成功: {milvus_config.feature_index_type}")
        except Exception as e:
            logger.warning(f"音频指纹索引可能已存在: {e}")

    def _fingerprint_to_binary_array(self, fingerprint: bytes) -> np.ndarray:
        bits = np.unpackbits(np.frombuffer(fingerprint, dtype=np.uint8))
        return bits.astype(np.float32).reshape(1, -1)

    def insert_multimodal(self, video_id: str,
                         multimodal_fps: List[MultimodalFingerprint]) -> List[int]:
        if not multimodal_fps:
            logger.warning("没有多模态指纹需要插入")
            return []

        batch_size = 1000
        all_ids = []
        
        for i in range(0, len(multimodal_fps), batch_size):
            batch = multimodal_fps[i:i + batch_size]
            
            binary_vectors = []
            feature_vectors = []
            audio_vectors = []
            timestamps = []
            frame_indices = []
            
            for fp in batch:
                fp_bytes = fp.fingerprint
                if len(fp_bytes) * 8 != self.fingerprint_dim:
                    logger.warning(f"指纹维度不匹配: {len(fp_bytes)*8} != {self.fingerprint_dim}")
                    fp_bytes = fp_bytes[:self.fingerprint_dim // 8] if len(fp_bytes) > self.fingerprint_dim // 8 else fp_bytes.ljust(self.fingerprint_dim // 8, b'\x00')
                binary_vectors.append(fp_bytes)
                
                if fp.feature is not None:
                    feature_vectors.append(fp.feature.astype(np.float32))
                else:
                    feature_vectors.append(np.zeros(self.feature_dim, dtype=np.float32))
                
                if fp.audio is not None:
                    audio_vectors.append(fp.audio.astype(np.float32))
                else:
                    audio_vectors.append(np.zeros(self.audio_dim, dtype=np.float32))
                
                timestamps.append(fp.timestamp)
                frame_indices.append(fp.frame_index)
            
            binary_array = np.array(binary_vectors, dtype=f'|V{self.fingerprint_dim // 8}')
            feature_array = np.vstack(feature_vectors)
            audio_array = np.vstack(audio_vectors)
            
            data = [
                [video_id] * len(batch),
                frame_indices,
                timestamps,
                binary_array,
                feature_array,
                audio_array,
            ]
            
            try:
                result = self.collection.insert(data)
                all_ids.extend(result.primary_keys)
                logger.info(f"插入批次 {i//batch_size + 1}: {len(batch)} 条多模态指纹")
            except Exception as e:
                logger.error(f"插入批次失败: {e}")
                raise
        
        self.collection.flush()
        return all_ids

    def insert_fingerprints(self, video_id: str,
                           fingerprints: List[bytes],
                           timestamps: List[float],
                           features: Optional[List[np.ndarray]] = None,
                           audios: Optional[List[np.ndarray]] = None) -> List[int]:
        if not fingerprints:
            logger.warning("没有指纹需要插入")
            return []

        multimodal_fps = []
        for i, (fp, ts) in enumerate(zip(fingerprints, timestamps)):
            feature = features[i] if features and i < len(features) else None
            audio = audios[i] if audios and i < len(audios) else None
            multimodal_fps.append(MultimodalFingerprint(
                fingerprint=fp,
                feature=feature,
                audio=audio,
                timestamp=ts,
                frame_index=i
            ))
        
        return self.insert_multimodal(video_id, multimodal_fps)

    def search_similar(self, query_fingerprint: bytes,
                       top_k: int = 100,
                       video_id_filter: Optional[str] = None) -> List[Dict]:
        if len(query_fingerprint) * 8 != self.fingerprint_dim:
            query_fingerprint = query_fingerprint[:self.fingerprint_dim // 8] if len(query_fingerprint) > self.fingerprint_dim // 8 else query_fingerprint.ljust(self.fingerprint_dim // 8, b'\x00')
        
        binary_array = np.array([query_fingerprint], dtype=f'|V{self.fingerprint_dim // 8}')
        
        search_params = {
            "metric_type": milvus_config.fingerprint_metric,
            "params": {}
        }
        
        expr = f'video_id == "{video_id_filter}"' if video_id_filter else None
        
        try:
            results = self.collection.search(
                data=binary_array,
                anns_field="fingerprint",
                param=search_params,
                limit=top_k,
                expr=expr,
                output_fields=["video_id", "frame_index", "timestamp"]
            )
            
            matches = []
            for hits in results:
                for hit in hits:
                    matches.append({
                        "video_id": hit.entity.get("video_id"),
                        "frame_index": hit.entity.get("frame_index"),
                        "timestamp": hit.entity.get("timestamp"),
                        "distance": hit.distance,
                        "similarity": 1.0 - (hit.distance / self.fingerprint_dim),
                        "field": "fingerprint"
                    })
            
            return matches
        except Exception as e:
            logger.error(f"指纹搜索失败: {e}")
            return []

    def search_by_feature(self, query_feature: np.ndarray,
                         top_k: int = 100,
                         video_id_filter: Optional[str] = None) -> List[Dict]:
        if query_feature.ndim == 1:
            query_feature = query_feature.reshape(1, -1)
        
        search_params = {
            "metric_type": milvus_config.feature_metric,
            "params": {"nprobe": milvus_config.nprobe}
        }
        
        expr = f'video_id == "{video_id_filter}"' if video_id_filter else None
        
        try:
            results = self.collection.search(
                data=query_feature.astype(np.float32),
                anns_field="feature",
                param=search_params,
                limit=top_k,
                expr=expr,
                output_fields=["video_id", "frame_index", "timestamp"]
            )
            
            matches = []
            for hits in results:
                for hit in hits:
                    matches.append({
                        "video_id": hit.entity.get("video_id"),
                        "frame_index": hit.entity.get("frame_index"),
                        "timestamp": hit.entity.get("timestamp"),
                        "distance": hit.distance,
                        "similarity": (1.0 + hit.distance) / 2.0 if milvus_config.feature_metric == "COSINE" else 1.0 - hit.distance,
                        "field": "feature"
                    })
            
            return matches
        except Exception as e:
            logger.error(f"深度特征搜索失败: {e}")
            return []

    def search_by_audio(self, query_audio: np.ndarray,
                       top_k: int = 100,
                       video_id_filter: Optional[str] = None) -> List[Dict]:
        if query_audio.ndim == 1:
            query_audio = query_audio.reshape(1, -1)
        
        search_params = {
            "metric_type": milvus_config.audio_metric,
            "params": {"nprobe": milvus_config.nprobe}
        }
        
        expr = f'video_id == "{video_id_filter}"' if video_id_filter else None
        
        try:
            results = self.collection.search(
                data=query_audio.astype(np.float32),
                anns_field="audio",
                param=search_params,
                limit=top_k,
                expr=expr,
                output_fields=["video_id", "frame_index", "timestamp"]
            )
            
            matches = []
            for hits in results:
                for hit in hits:
                    matches.append({
                        "video_id": hit.entity.get("video_id"),
                        "frame_index": hit.entity.get("frame_index"),
                        "timestamp": hit.entity.get("timestamp"),
                        "distance": hit.distance,
                        "similarity": (1.0 + hit.distance) / 2.0 if milvus_config.audio_metric == "COSINE" else 1.0 - hit.distance,
                        "field": "audio"
                    })
            
            return matches
        except Exception as e:
            logger.error(f"音频指纹搜索失败: {e}")
            return []

    def batch_search(self, query_fingerprints: List[bytes],
                     top_k: int = 10) -> List[List[Dict]]:
        binary_vectors = []
        for fp in query_fingerprints:
            if len(fp) * 8 != self.fingerprint_dim:
                fp = fp[:self.fingerprint_dim // 8] if len(fp) > self.fingerprint_dim // 8 else fp.ljust(self.fingerprint_dim // 8, b'\x00')
            binary_vectors.append(fp)
        
        binary_array = np.array(binary_vectors, dtype=f'|V{self.fingerprint_dim // 8}')
        
        search_params = {
            "metric_type": milvus_config.fingerprint_metric,
            "params": {}
        }
        
        try:
            results = self.collection.search(
                data=binary_array,
                anns_field="fingerprint",
                param=search_params,
                limit=top_k,
                output_fields=["video_id", "frame_index", "timestamp"]
            )
            
            all_matches = []
            for hits in results:
                matches = []
                for hit in hits:
                    matches.append({
                        "video_id": hit.entity.get("video_id"),
                        "frame_index": hit.entity.get("frame_index"),
                        "timestamp": hit.entity.get("timestamp"),
                        "distance": hit.distance,
                        "similarity": 1.0 - (hit.distance / self.fingerprint_dim)
                    })
                all_matches.append(matches)
            
            return all_matches
        except Exception as e:
            logger.error(f"指纹批量搜索失败: {e}")
            return [[] for _ in query_fingerprints]

    def batch_search_multimodal(self,
                               fingerprints: Optional[List[bytes]] = None,
                               features: Optional[np.ndarray] = None,
                               audios: Optional[np.ndarray] = None,
                               top_k: int = 10) -> Dict[str, List[List[Dict]]]:
        results = {}
        
        if fingerprints is not None:
            results["fingerprint"] = self.batch_search(fingerprints, top_k=top_k)
        
        if features is not None:
            feat_results = []
            for feat in features:
                feat_results.append(self.search_by_feature(feat, top_k=top_k))
            results["feature"] = feat_results
        
        if audios is not None:
            audio_results = []
            for audio in audios:
                audio_results.append(self.search_by_audio(audio, top_k=top_k))
            results["audio"] = audio_results
        
        return results

    def delete_by_video_id(self, video_id: str) -> bool:
        try:
            expr = f'video_id == "{video_id}"'
            self.collection.delete(expr)
            self.collection.flush()
            logger.info(f"已删除视频 {video_id} 的所有指纹")
            return True
        except Exception as e:
            logger.error(f"删除失败: {e}")
            return False

    def get_video_fingerprints(self, video_id: str) -> List[Dict]:
        try:
            expr = f'video_id == "{video_id}"'
            results = self.collection.query(
                expr=expr,
                output_fields=["video_id", "frame_index", "timestamp", "fingerprint", "feature", "audio"]
            )
            
            fps_list = []
            for res in results:
                fp_bytes = bytes(res["fingerprint"])
                fps_list.append({
                    "video_id": res["video_id"],
                    "frame_index": res["frame_index"],
                    "timestamp": res["timestamp"],
                    "fingerprint": fp_bytes,
                    "feature": np.array(res["feature"], dtype=np.float32),
                    "audio": np.array(res["audio"], dtype=np.float32)
                })
            
            fps_list.sort(key=lambda x: x["frame_index"])
            return fps_list
        except Exception as e:
            logger.error(f"查询失败: {e}")
            return []

    def get_video_multimodal(self, video_id: str) -> List[MultimodalFingerprint]:
        fps_data = self.get_video_fingerprints(video_id)
        return [
            MultimodalFingerprint(
                fingerprint=d["fingerprint"],
                feature=d["feature"],
                audio=d["audio"],
                timestamp=d["timestamp"],
                frame_index=d["frame_index"]
            )
            for d in fps_data
        ]

    def count(self) -> int:
        try:
            return self.collection.num_entities
        except Exception as e:
            logger.error(f"统计失败: {e}")
            return 0

    def clear(self):
        if utility.has_collection(self.collection_name):
            utility.drop_collection(self.collection_name)
            logger.info(f"已删除集合: {self.collection_name}")
            self._init_collection()

    def close(self):
        if self.collection:
            self.collection.release()
        connections.disconnect("default")
        logger.info("已断开Milvus连接")
