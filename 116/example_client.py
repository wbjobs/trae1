import httpx
import asyncio
import json
import os
from typing import Optional, Dict, Any, List


class VideoFingerprintClient:
    def __init__(self, base_url: str = "http://localhost:8000"):
        self.base_url = base_url

    async def extract_fingerprints(
        self, video_url: str, video_id: Optional[str] = None,
        enable_deep_feature: bool = True, enable_audio: bool = True,
        export_features: bool = False
    ) -> Dict[str, Any]:
        async with httpx.AsyncClient(timeout=300.0) as client:
            payload = {
                "video_url": video_url,
                "enable_deep_feature": enable_deep_feature,
                "enable_audio": enable_audio,
                "export_features": export_features
            }
            if video_id:
                payload["video_id"] = video_id
            
            response = await client.post(
                f"{self.base_url}/api/v1/extract",
                json=payload
            )
            response.raise_for_status()
            return response.json()

    async def query_by_url(
        self, video_url: str, max_duration: int = 30,
        top_k: int = 10, use_multimodal: bool = True,
        enable_deep_feature: bool = True, enable_audio: bool = True
    ) -> Dict[str, Any]:
        async with httpx.AsyncClient(timeout=300.0) as client:
            payload = {
                "video_url": video_url,
                "max_duration": max_duration,
                "top_k": top_k,
                "use_multimodal": use_multimodal,
                "enable_deep_feature": enable_deep_feature,
                "enable_audio": enable_audio
            }
            
            response = await client.post(
                f"{self.base_url}/api/v1/query",
                json=payload
            )
            response.raise_for_status()
            return response.json()

    async def query_by_upload(
        self, file_path: str, max_duration: int = 30,
        top_k: int = 10, use_multimodal: bool = True,
        enable_deep_feature: bool = True, enable_audio: bool = True
    ) -> Dict[str, Any]:
        async with httpx.AsyncClient(timeout=300.0) as client:
            with open(file_path, "rb") as f:
                files = {"file": f}
                params = {
                    "max_duration": max_duration,
                    "top_k": top_k,
                    "use_multimodal": use_multimodal,
                    "enable_deep_feature": enable_deep_feature,
                    "enable_audio": enable_audio
                }
                
                response = await client.post(
                    f"{self.base_url}/api/v1/query/upload",
                    files=files,
                    params=params
                )
                response.raise_for_status()
                return response.json()

    async def export_features(
        self, video_id: str, format: str = "numpy",
        include_frames: bool = False
    ) -> Dict[str, Any]:
        async with httpx.AsyncClient() as client:
            params = {"format": format, "include_frames": include_frames}
            response = await client.get(
                f"{self.base_url}/api/v1/export/{video_id}",
                params=params
            )
            response.raise_for_status()
            return response.json()

    async def download_exported_features(
        self, video_id: str, output_dir: str = ".", format: str = "numpy"
    ) -> str:
        async with httpx.AsyncClient() as client:
            params = {"format": format}
            response = await client.get(
                f"{self.base_url}/api/v1/export/download/{video_id}",
                params=params
            )
            response.raise_for_status()
            
            os.makedirs(output_dir, exist_ok=True)
            filename = f"{video_id}_features.{format}"
            output_path = os.path.join(output_dir, filename)
            
            with open(output_path, "wb") as f:
                f.write(response.content)
            
            return output_path

    async def get_fusion_config(self) -> Dict[str, Any]:
        async with httpx.AsyncClient() as client:
            response = await client.get(f"{self.base_url}/api/v1/fusion/config")
            response.raise_for_status()
            return response.json()

    async def set_fusion_config(
        self, fingerprint_weight: float, feature_weight: float, audio_weight: float
    ) -> Dict[str, Any]:
        async with httpx.AsyncClient() as client:
            payload = {
                "fingerprint_weight": fingerprint_weight,
                "feature_weight": feature_weight,
                "audio_weight": audio_weight
            }
            response = await client.post(
                f"{self.base_url}/api/v1/fusion/config",
                json=payload
            )
            response.raise_for_status()
            return response.json()

    async def delete_video(self, video_id: str) -> Dict[str, Any]:
        async with httpx.AsyncClient() as client:
            response = await client.delete(
                f"{self.base_url}/api/v1/video/{video_id}"
            )
            response.raise_for_status()
            return response.json()

    async def get_status(self) -> Dict[str, Any]:
        async with httpx.AsyncClient() as client:
            response = await client.get(f"{self.base_url}/api/v1/status")
            response.raise_for_status()
            return response.json()

    async def get_video_count(self, video_id: str) -> Dict[str, Any]:
        async with httpx.AsyncClient() as client:
            response = await client.get(
                f"{self.base_url}/api/v1/video/{video_id}/count"
            )
            response.raise_for_status()
            return response.json()

    async def health_check(self) -> Dict[str, Any]:
        async with httpx.AsyncClient() as client:
            response = await client.get(f"{self.base_url}/health")
            response.raise_for_status()
            return response.json()


async def main():
    client = VideoFingerprintClient("http://localhost:8000")
    
    print("=" * 70)
    print("多模态视频指纹服务 API 示例 v2.0")
    print("=" * 70)
    
    try:
        print("\n1. 健康检查")
        health = await client.health_check()
        print(f"   状态: {json.dumps(health, indent=2, ensure_ascii=False)}")
        
        print("\n2. 获取服务状态")
        status = await client.get_status()
        print(f"   总指纹数: {status['total_fingerprints']}")
        print(f"   深度特征提取: {'启用' if status['deep_feature_enabled'] else '禁用'}")
        print(f"   音频指纹提取: {'启用' if status['audio_enabled'] else '禁用'}")
        print(f"   融合权重: {json.dumps(status['fusion_weights'], indent=4, ensure_ascii=False)}")
        
        print("\n3. 获取融合权重配置")
        fusion_config = await client.get_fusion_config()
        print(f"   配置: {json.dumps(fusion_config, indent=2, ensure_ascii=False)}")
        
        print("\n4. 修改融合权重示例 (fp=0.2, feat=0.7, audio=0.1)")
        try:
            new_config = await client.set_fusion_config(0.2, 0.7, 0.1)
            print(f"   新配置: {json.dumps(new_config, indent=2, ensure_ascii=False)}")
            
            print("   恢复默认权重...")
            await client.set_fusion_config(0.2, 0.6, 0.2)
        except Exception as e:
            print(f"   跳过权重修改: {e}")
        
        print("\n5. 提取视频指纹（多模态 + 特征导出）示例")
        print("   请确保有可用的视频源...")
        video_url = input("   请输入视频URL或文件路径 (直接回车跳过): ").strip()
        
        if video_url:
            video_id = input("   请输入视频ID (可选，回车自动生成): ").strip() or None
            export_feats = input("   是否导出特征向量? (y/N): ").strip().lower() == 'y'
            
            print("   正在提取多模态指纹...")
            extract_result = await client.extract_fingerprints(
                video_url, video_id,
                enable_deep_feature=True,
                enable_audio=True,
                export_features=export_feats
            )
            print(f"   提取结果: {json.dumps(extract_result, indent=2, ensure_ascii=False)}")
            
            actual_video_id = extract_result["video_id"]
            
            if export_feats and extract_result.get("export_path"):
                print(f"\n6. 下载导出的特征文件")
                try:
                    download_path = await client.download_exported_features(
                        actual_video_id, output_dir="./exports", format="numpy"
                    )
                    print(f"   特征文件已下载到: {download_path}")
                except Exception as e:
                    print(f"   下载跳过: {e}")
            
            print(f"\n7. 查询视频 {actual_video_id} 的指纹数量")
            count_result = await client.get_video_count(actual_video_id)
            print(f"   结果: {json.dumps(count_result, indent=2, ensure_ascii=False)}")
        
        print("\n8. Top-K 多模态查询示例")
        query_url = input("   请输入查询视频URL (直接回车跳过): ").strip()
        
        if query_url:
            top_k = int(input("   返回Top-K结果 (默认10): ").strip() or "10")
            use_multimodal = input("   使用多模态匹配? (Y/n): ").strip().lower() != 'n'
            
            print(f"   正在查询 (Top-{top_k}, 多模态={'是' if use_multimodal else '否'})...")
            query_result = await client.query_by_url(
                query_url, max_duration=30,
                top_k=top_k, use_multimodal=use_multimodal
            )
            print(f"   查询模式: {query_result.get('search_mode', 'unknown')}")
            print(f"   处理时间: {query_result['processing_time']:.3f}秒")
            
            if query_result["results"]:
                print(f"\n   Top-{len(query_result['results'])} 匹配结果:")
                for i, match in enumerate(query_result["results"], 1):
                    print(f"\n   {i}. 视频ID: {match['video_id']}")
                    print(f"      置信度: {match['confidence']:.4f}")
                    print(f"      原始分数: {match.get('raw_score', 0):.4f}")
                    print(f"      匹配时间戳: {match['start_timestamp']:.2f}s - {match['end_timestamp']:.2f}s")
                    print(f"      查询片段长度: {match['query_length']:.2f}s")
                    print(f"      使用模态: {match.get('modalities', [])}")
            else:
                print("   未找到匹配结果")
        
        print("\n" + "=" * 70)
        print("示例执行完成")
        print("=" * 70)
        
    except httpx.HTTPError as e:
        print(f"\nHTTP请求错误: {e}")
        print("请确保服务已启动: python main.py")
    except Exception as e:
        print(f"\n错误: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    asyncio.run(main())
