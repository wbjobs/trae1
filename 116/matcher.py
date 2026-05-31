import numpy as np
from typing import List, Dict, Optional, Tuple
from collections import defaultdict, Counter
from loguru import logger
from config import matching_config, video_config, fusion_config
from fingerprint_generator import FingerprintGenerator, MultimodalFingerprint
from milvus_store import MilvusStore
from deep_feature_extractor import DeepFeatureExtractor
from audio_fingerprint import AudioFingerprintExtractor


class FingerprintMatcher:
    def __init__(self, milvus_store: MilvusStore,
                 fingerprint_generator: FingerprintGenerator,
                 deep_feature_extractor: Optional[DeepFeatureExtractor] = None,
                 audio_extractor: Optional[AudioFingerprintExtractor] = None,
                 similarity_threshold: float = matching_config.similarity_threshold,
                 max_results: int = matching_config.max_results,
                 time_tolerance: float = matching_config.time_tolerance,
                 sliding_window_step: int = matching_config.sliding_window_step,
                 top_k_candidates: int = matching_config.top_k_candidates):
        self.milvus_store = milvus_store
        self.fp_generator = fingerprint_generator
        self.deep_feature_extractor = deep_feature_extractor
        self.audio_extractor = audio_extractor
        self.similarity_threshold = similarity_threshold
        self.max_results = max_results
        self.time_tolerance = time_tolerance
        self.sliding_window_step = sliding_window_step
        self.top_k_candidates = top_k_candidates
        self.fp_dim = video_config.fingerprint_bytes * 8
        self.fp_weight = fusion_config.fingerprint_weight
        self.feat_weight = fusion_config.feature_weight
        self.audio_weight = fusion_config.audio_weight
        self.enable_audio = fusion_config.enable_audio

    def match_sequence(self, query_fingerprints: List[bytes],
                       query_timestamps: Optional[List[float]] = None) -> List[Dict]:
        if not query_fingerprints:
            return []

        query_len = len(query_fingerprints)
        logger.info(f"开始匹配查询序列，长度: {query_len} 帧")

        candidate_matches = self._search_candidates(query_fingerprints)
        
        if not candidate_matches:
            logger.info("未找到候选匹配")
            return []

        video_candidates = self._group_by_video(candidate_matches, query_len)
        
        results = []
        for video_id, candidate_data in video_candidates.items():
            match_result = self._verify_sequence_match(
                video_id,
                query_fingerprints,
                candidate_data,
                query_len
            )
            if match_result:
                results.append(match_result)

        results.sort(key=lambda x: x["confidence"], reverse=True)
        return results[:self.max_results]

    def match_multimodal(self,
                        query_multimodal: List[MultimodalFingerprint],
                        top_k: Optional[int] = None) -> List[Dict]:
        if not query_multimodal:
            return []

        query_len = len(query_multimodal)
        top_k = top_k or self.max_results
        logger.info(f"开始多模态匹配，查询长度: {query_len} 帧, 返回Top-{top_k}")

        query_fingerprints = [m.fingerprint for m in query_multimodal]
        query_features = np.array([m.feature for m in query_multimodal if m.feature is not None])
        query_audios = np.array([m.audio for m in query_multimodal if m.audio is not None])

        candidate_matches = self._search_candidates_multimodal(
            query_fingerprints,
            query_features if len(query_features) > 0 else None,
            query_audios if len(query_audios) > 0 else None
        )
        
        if not candidate_matches:
            logger.info("未找到多模态候选匹配")
            return []

        video_candidates = self._group_by_video_multimodal(candidate_matches, query_len)
        
        results = []
        for video_id, candidate_data in video_candidates.items():
            match_result = self._verify_multimodal_match(
                video_id,
                query_multimodal,
                candidate_data,
                query_len
            )
            if match_result:
                results.append(match_result)

        results.sort(key=lambda x: x["confidence"], reverse=True)
        return results[:top_k]

    def _search_candidates(self, query_fingerprints: List[bytes]) -> List[List[Dict]]:
        search_top_k = min(self.top_k_candidates, max(20, len(query_fingerprints) * 5))
        logger.info(f"批量搜索指纹候选，top_k={search_top_k}")
        
        all_matches = self.milvus_store.batch_search(
            query_fingerprints,
            top_k=search_top_k
        )
        
        filtered_matches = []
        for matches in all_matches:
            filtered = [m for m in matches if m["similarity"] >= self.similarity_threshold * 0.6]
            filtered_matches.append(filtered)
        
        return filtered_matches

    def _search_candidates_multimodal(self,
                                       fingerprints: List[bytes],
                                       features: Optional[np.ndarray] = None,
                                       audios: Optional[np.ndarray] = None) -> Dict[str, List[List[Dict]]]:
        search_top_k = min(self.top_k_candidates, max(20, len(fingerprints) * 3))
        logger.info(f"多模态候选搜索，top_k={search_top_k}")
        
        results = {}
        
        if features is not None and self.feat_weight > 0:
            logger.info(f"使用深度特征搜索候选...")
            feat_matches = []
            for feat in features:
                fm = self.milvus_store.search_by_feature(feat, top_k=search_top_k)
                feat_matches.append(fm)
            results["feature"] = feat_matches
        
        if self.fp_weight > 0:
            logger.info(f"使用pHash指纹搜索候选...")
            fp_matches = []
            for fp in fingerprints:
                fm = self.milvus_store.search_similar(fp, top_k=search_top_k)
                fp_matches.append(fm)
            results["fingerprint"] = fp_matches
        
        if audios is not None and self.audio_weight > 0 and self.enable_audio:
            logger.info(f"使用音频指纹搜索候选...")
            audio_matches = []
            for audio in audios:
                am = self.milvus_store.search_by_audio(audio, top_k=search_top_k)
                audio_matches.append(am)
            results["audio"] = audio_matches
        
        return results

    def _group_by_video(self, candidate_matches: List[List[Dict]],
                        query_len: int) -> Dict[str, Dict]:
        video_hits = defaultdict(lambda: {"matches": [], "hit_count": 0})
        
        for query_idx, frame_matches in enumerate(candidate_matches):
            for match in frame_matches:
                video_id = match["video_id"]
                video_hits[video_id]["matches"].append({
                    "query_idx": query_idx,
                    "match": match
                })
                video_hits[video_id]["hit_count"] += 1

        min_hits = max(3, query_len // 4)
        filtered = {
            vid: data for vid, data in video_hits.items()
            if data["hit_count"] >= min_hits
        }

        logger.info(f"找到 {len(filtered)} 个候选视频，命中数 >= {min_hits}")
        return filtered

    def _group_by_video_multimodal(self, candidate_matches: Dict[str, List[List[Dict]]],
                                    query_len: int) -> Dict[str, Dict]:
        video_hits = defaultdict(lambda: {"matches": defaultdict(list), "hit_count": 0, "modalities": set()})
        
        for modality, frame_matches_list in candidate_matches.items():
            for query_idx, frame_matches in enumerate(frame_matches_list):
                for match in frame_matches:
                    video_id = match["video_id"]
                    video_hits[video_id]["matches"][query_idx].append({
                        "modality": modality,
                        "match": match
                    })
                    video_hits[video_id]["hit_count"] += 1
                    video_hits[video_id]["modalities"].add(modality)

        min_hits = max(3, query_len // 4)
        filtered = {}
        for vid, data in video_hits.items():
            if data["hit_count"] >= min_hits:
                hit_indices = sorted(data["matches"].keys())
                coverage = len(hit_indices) / query_len
                modality_count = len(data["modalities"])
                
                score = (data["hit_count"] * 0.4 + 
                        coverage * 0.3 + 
                        modality_count * 0.3)
                
                filtered[vid] = {
                    "matches": data["matches"],
                    "hit_count": data["hit_count"],
                    "modalities": data["modalities"],
                    "coverage": coverage,
                    "score": score
                }

        filtered = dict(sorted(filtered.items(), key=lambda x: x[1]["score"], reverse=True))
        
        logger.info(f"找到 {len(filtered)} 个多模态候选视频，命中数 >= {min_hits}")
        return filtered

    def _verify_sequence_match(self, video_id: str,
                               query_fingerprints: List[bytes],
                               candidate_data: Dict,
                               query_len: int) -> Optional[Dict]:
        matches = candidate_data["matches"]
        
        offset_scores = self._calculate_offset_scores(matches, query_len)
        
        if not offset_scores:
            return None

        best_offset, best_score = max(offset_scores.items(), key=lambda x: x[1])
        
        if best_score < self.similarity_threshold:
            return None

        confidence, matched_range = self._calculate_sequence_similarity(
            video_id,
            query_fingerprints,
            best_offset,
            query_len
        )

        if confidence < self.similarity_threshold:
            return None

        start_timestamp = best_offset * video_config.frame_interval
        end_timestamp = start_timestamp + (query_len - 1) * video_config.frame_interval

        return {
            "video_id": video_id,
            "confidence": round(confidence, 4),
            "start_timestamp": round(start_timestamp, 2),
            "end_timestamp": round(end_timestamp, 2),
            "matched_frames": matched_range,
            "query_length": query_len
        }

    def _calculate_offset_scores(self, matches: List[Dict],
                                 query_len: int) -> Dict[int, float]:
        offset_counts = defaultdict(list)

        for m in matches:
            query_idx = m["query_idx"]
            match = m["match"]
            frame_idx = match["frame_index"]
            offset = frame_idx - query_idx
            offset_counts[offset].append(match["similarity"])

        min_occurrences = max(3, query_len // 5)
        offset_scores = {}
        
        for offset, similarities in offset_counts.items():
            if len(similarities) >= min_occurrences:
                avg_sim = np.mean(similarities)
                coverage = len(similarities) / query_len
                score = avg_sim * 0.6 + coverage * 0.4
                offset_scores[offset] = score

        return offset_scores

    def _calculate_sequence_similarity(self, video_id: str,
                                       query_fingerprints: List[bytes],
                                       start_offset: int,
                                       query_len: int) -> Tuple[float, Tuple[int, int]]:
        video_fps = self.milvus_store.get_video_fingerprints(video_id)
        if not video_fps:
            return 0.0, (0, 0)

        fp_map = {fp["frame_index"]: fp for fp in video_fps}
        
        similarities = []
        matched_start = None
        matched_end = None

        for i, query_fp in enumerate(query_fingerprints):
            target_idx = start_offset + i
            if target_idx in fp_map:
                video_fp = fp_map[target_idx]["fingerprint"]
                sim = self.fp_generator.similarity(query_fp, video_fp)
                similarities.append(sim)
                
                if sim >= self.similarity_threshold * 0.8:
                    if matched_start is None:
                        matched_start = i
                    matched_end = i

        if not similarities:
            return 0.0, (0, 0)

        avg_sim = np.mean(similarities)
        coverage = len(similarities) / query_len
        
        continuity_bonus = 0.0
        if matched_start is not None and matched_end is not None:
            matched_len = matched_end - matched_start + 1
            continuity = matched_len / query_len
            continuity_bonus = continuity * 0.2

        confidence = avg_sim * 0.6 + coverage * 0.2 + continuity_bonus
        confidence = min(1.0, confidence)

        matched_range = (matched_start or 0, matched_end or query_len - 1)
        return confidence, matched_range

    def _verify_multimodal_match(self, video_id: str,
                                  query_multimodal: List[MultimodalFingerprint],
                                  candidate_data: Dict,
                                  query_len: int) -> Optional[Dict]:
        matches_by_idx = candidate_data["matches"]
        
        offset_scores = self._calculate_multimodal_offset_scores(matches_by_idx, query_len)
        
        if not offset_scores:
            return None

        best_offset, best_score = max(offset_scores.items(), key=lambda x: x[1])
        
        if best_score < self.similarity_threshold * 0.7:
            return None

        confidence, matched_range = self._calculate_multimodal_similarity(
            video_id,
            query_multimodal,
            best_offset,
            query_len
        )

        if confidence < self.similarity_threshold:
            return None

        start_timestamp = best_offset * video_config.frame_interval
        end_timestamp = start_timestamp + (query_len - 1) * video_config.frame_interval

        return {
            "video_id": video_id,
            "confidence": round(confidence, 4),
            "start_timestamp": round(start_timestamp, 2),
            "end_timestamp": round(end_timestamp, 2),
            "matched_frames": matched_range,
            "query_length": query_len,
            "modalities": list(candidate_data["modalities"]),
            "raw_score": round(best_score, 4)
        }

    def _calculate_multimodal_offset_scores(self, matches_by_idx: Dict[int, List[Dict]],
                                             query_len: int) -> Dict[int, float]:
        offset_scores = defaultdict(lambda: {"total": 0.0, "count": 0, "modalities": set()})

        for query_idx, modality_matches in matches_by_idx.items():
            for m in modality_matches:
                modality = m["modality"]
                match = m["match"]
                frame_idx = match["frame_index"]
                offset = frame_idx - query_idx
                
                weight = 1.0
                if modality == "feature":
                    weight = self.feat_weight
                elif modality == "fingerprint":
                    weight = self.fp_weight
                elif modality == "audio":
                    weight = self.audio_weight
                
                offset_scores[offset]["total"] += match["similarity"] * weight
                offset_scores[offset]["count"] += 1
                offset_scores[offset]["modalities"].add(modality)

        min_occurrences = max(3, query_len // 5)
        final_scores = {}
        
        for offset, data in offset_scores.items():
            if data["count"] >= min_occurrences:
                avg_sim = data["total"] / data["count"]
                coverage = data["count"] / query_len
                modality_bonus = len(data["modalities"]) * 0.1
                score = avg_sim * 0.5 + coverage * 0.3 + modality_bonus
                final_scores[offset] = min(1.0, score)

        return final_scores

    def _calculate_multimodal_similarity(self, video_id: str,
                                          query_multimodal: List[MultimodalFingerprint],
                                          start_offset: int,
                                          query_len: int) -> Tuple[float, Tuple[int, int]]:
        video_mm = self.milvus_store.get_video_multimodal(video_id)
        if not video_mm:
            return 0.0, (0, 0)

        video_fps_map = {fp.frame_index: fp for fp in video_mm}
        
        similarities = []
        matched_start = None
        matched_end = None

        for i, query_fp in enumerate(query_multimodal):
            target_idx = start_offset + i
            if target_idx in video_fps_map:
                video_fp = video_fps_map[target_idx]
                sim = self.fp_generator.fused_similarity(query_fp, video_fp)
                similarities.append(sim)
                
                if sim >= self.similarity_threshold * 0.8:
                    if matched_start is None:
                        matched_start = i
                    matched_end = i

        if not similarities:
            return 0.0, (0, 0)

        avg_sim = np.mean(similarities)
        coverage = len(similarities) / query_len
        
        continuity_bonus = 0.0
        if matched_start is not None and matched_end is not None:
            matched_len = matched_end - matched_start + 1
            continuity = matched_len / query_len
            continuity_bonus = continuity * 0.15

        confidence = avg_sim * 0.65 + coverage * 0.2 + continuity_bonus
        confidence = min(1.0, confidence)

        matched_range = (matched_start or 0, matched_end or query_len - 1)
        return confidence, matched_range

    def fast_match_multimodal(self, query_multimodal: List[MultimodalFingerprint],
                              top_k: Optional[int] = None) -> List[Dict]:
        if len(query_multimodal) <= 10:
            return self.match_multimodal(query_multimodal, top_k)
        else:
            step = max(1, len(query_multimodal) // 10)
            sampled = query_multimodal[::step]
            quick_results = self.match_multimodal(sampled, top_k=3)
            
            if not quick_results:
                return []
            
            top_candidates = [r["video_id"] for r in quick_results[:3]]
            detailed_results = []
            
            for video_id in top_candidates:
                video_mm = self.milvus_store.get_video_multimodal(video_id)
                if not video_mm:
                    continue
                
                result = self._direct_multimodal_match(query_multimodal, video_mm)
                if result:
                    detailed_results.append(result)
            
            detailed_results.sort(key=lambda x: x["confidence"], reverse=True)
            return detailed_results[:(top_k or self.max_results)]

    def _direct_multimodal_match(self, query_mm: List[MultimodalFingerprint],
                                  video_mm_list: List[MultimodalFingerprint]) -> Optional[Dict]:
        video_mm_map = {fp.frame_index: fp for fp in video_mm_list}
        video_indices = sorted(video_mm_map.keys())
        
        if not video_indices:
            return None

        query_len = len(query_mm)
        best_confidence = 0.0
        best_offset = 0

        for start_idx in range(0, len(video_indices) - query_len + 1, self.sliding_window_step):
            offset = video_indices[start_idx]
            similarities = []
            
            for i in range(query_len):
                target_idx = offset + i
                if target_idx in video_mm_map:
                    sim = self.fp_generator.fused_similarity(query_mm[i], video_mm_map[target_idx])
                    similarities.append(sim)
            
            if similarities:
                confidence = np.mean(similarities) * (len(similarities) / query_len)
                if confidence > best_confidence:
                    best_confidence = confidence
                    best_offset = offset

        if best_confidence < self.similarity_threshold:
            return None

        start_timestamp = best_offset * video_config.frame_interval
        end_timestamp = start_timestamp + (query_len - 1) * video_config.frame_interval

        return {
            "video_id": video_mm_list[0].video_id if hasattr(video_mm_list[0], 'video_id') else video_mm_list[0].frame_index,
            "confidence": round(best_confidence, 4),
            "start_timestamp": round(start_timestamp, 2),
            "end_timestamp": round(end_timestamp, 2),
            "matched_frames": (0, query_len - 1),
            "query_length": query_len
        }

    def sliding_window_match(self, query_fingerprints: List[bytes]) -> List[Dict]:
        window_size = min(len(query_fingerprints), 30)
        results = []

        for start in range(0, len(query_fingerprints) - window_size + 1, self.sliding_window_step):
            window_fps = query_fingerprints[start:start + window_size]
            window_results = self.match_sequence(window_fps)
            
            for res in window_results:
                res["query_start_idx"] = start
                res["query_end_idx"] = start + window_size
                results.append(res)

        return self._merge_overlapping_results(results)

    def _merge_overlapping_results(self, results: List[Dict]) -> List[Dict]:
        if not results:
            return []

        results.sort(key=lambda x: (x["video_id"], x["start_timestamp"]))
        
        merged = []
        current_group = [results[0]]

        for res in results[1:]:
            last = current_group[-1]
            if (res["video_id"] == last["video_id"] and
                abs(res["start_timestamp"] - last["start_timestamp"]) <= self.time_tolerance):
                current_group.append(res)
            else:
                merged.append(self._merge_group(current_group))
                current_group = [res]

        merged.append(self._merge_group(current_group))
        merged.sort(key=lambda x: x["confidence"], reverse=True)
        
        return merged[:self.max_results]

    def _merge_group(self, group: List[Dict]) -> Dict:
        if not group:
            return {}
        
        best = max(group, key=lambda x: x["confidence"])
        
        all_starts = [g["start_timestamp"] for g in group]
        all_ends = [g["end_timestamp"] for g in group]
        
        return {
            "video_id": best["video_id"],
            "confidence": round(np.mean([g["confidence"] for g in group]), 4),
            "start_timestamp": round(min(all_starts), 2),
            "end_timestamp": round(max(all_ends), 2),
            "matched_frames": best["matched_frames"],
            "query_length": best["query_length"],
            "match_count": len(group)
        }

    def fast_match(self, query_fingerprints: List[bytes]) -> List[Dict]:
        if len(query_fingerprints) <= 10:
            return self.match_sequence(query_fingerprints)
        else:
            step = max(1, len(query_fingerprints) // 10)
            sampled_fps = query_fingerprints[::step]
            quick_results = self.match_sequence(sampled_fps)
            
            if not quick_results:
                return []
            
            top_candidates = [r["video_id"] for r in quick_results[:3]]
            detailed_results = []
            
            for video_id in top_candidates:
                video_fps = self.milvus_store.get_video_fingerprints(video_id)
                if not video_fps:
                    continue
                
                result = self._direct_sequence_match(
                    query_fingerprints,
                    video_fps
                )
                if result:
                    detailed_results.append(result)
            
            return detailed_results

    def _direct_sequence_match(self, query_fps: List[bytes],
                               video_fps_list: List[Dict]) -> Optional[Dict]:
        video_fps = {fp["frame_index"]: fp["fingerprint"] for fp in video_fps_list}
        video_indices = sorted(video_fps.keys())
        
        if not video_indices:
            return None

        query_len = len(query_fps)
        best_confidence = 0.0
        best_offset = 0

        for start_idx in range(0, len(video_indices) - query_len + 1, self.sliding_window_step):
            offset = video_indices[start_idx]
            similarities = []
            
            for i in range(query_len):
                target_idx = offset + i
                if target_idx in video_fps:
                    sim = self.fp_generator.similarity(query_fps[i], video_fps[target_idx])
                    similarities.append(sim)
            
            if similarities:
                confidence = np.mean(similarities) * (len(similarities) / query_len)
                if confidence > best_confidence:
                    best_confidence = confidence
                    best_offset = offset

        if best_confidence < self.similarity_threshold:
            return None

        start_timestamp = best_offset * video_config.frame_interval
        end_timestamp = start_timestamp + (query_len - 1) * video_config.frame_interval

        return {
            "video_id": video_fps_list[0]["video_id"],
            "confidence": round(best_confidence, 4),
            "start_timestamp": round(start_timestamp, 2),
            "end_timestamp": round(end_timestamp, 2),
            "matched_frames": (0, query_len - 1),
            "query_length": query_len
        }
