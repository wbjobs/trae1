import os
import json
import uuid
import httpx
import hashlib
import hmac
import cv2
import numpy as np
from datetime import datetime, timedelta
from typing import List, Dict, Optional, Any
from loguru import logger
from jinja2 import Template
from config import (
    InfringementRecord, 
    monitor_config, 
    webhook_config, 
    block_api_config
)


class EvidenceCollector:
    def __init__(self):
        self.evidence_dir = monitor_config.evidence_dir
        os.makedirs(self.evidence_dir, exist_ok=True)

    def capture_video_screenshot(self, video_url: str, output_path: str, 
                                 timestamp: float = 0) -> bool:
        cap = None
        try:
            cap = cv2.VideoCapture(video_url)
            if not cap.isOpened():
                logger.error(f"无法打开视频: {video_url}")
                return False

            fps = cap.get(cv2.CAP_PROP_FPS) or 25
            target_frame = int(timestamp * fps)
            cap.set(cv2.CAP_PROP_POS_FRAMES, target_frame)

            ret, frame = cap.read()
            if ret:
                cv2.imwrite(output_path, frame)
                logger.info(f"截图已保存: {output_path}")
                return True

        except Exception as e:
            logger.error(f"截图失败: {e}")
        finally:
            if cap:
                cap.release()

        return False

    def capture_multiple_screenshots(self, video_url: str, video_id: str,
                                     timestamps: List[float]) -> List[str]:
        screenshot_paths = []
        for i, ts in enumerate(timestamps):
            filename = f"{video_id}_screenshot_{i}_{int(ts)}s.jpg"
            output_path = os.path.join(self.evidence_dir, filename)
            if self.capture_video_screenshot(video_url, output_path, ts):
                screenshot_paths.append(output_path)
        return screenshot_paths

    def save_page_screenshot_url(self, video_url: str, video_id: str) -> str:
        url_filename = f"{video_id}_url_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"
        url_path = os.path.join(self.evidence_dir, url_filename)
        
        with open(url_path, 'w', encoding='utf-8') as f:
            f.write(f"侵权视频URL: {video_url}\n")
            f.write(f"取证时间: {datetime.now().isoformat()}\n")
            f.write(f"URL Hash: {hashlib.sha256(video_url.encode()).hexdigest()}\n")
        
        return url_path


class InfringementReportGenerator:
    def __init__(self):
        self.report_dir = monitor_config.report_dir
        os.makedirs(self.report_dir, exist_ok=True)
        
        self.report_template = Template("""
=============================================
                侵权检测报告
=============================================
报告编号: {{ record.id }}
生成时间: {{ report_time }}
检测时间: {{ record.detected_at }}

-------------- 侵权视频信息 --------------
视频ID: {{ record.video_id }}
视频标题: {{ record.video_title }}
视频URL: {{ record.video_url }}
发布平台: {{ record.platform }}
发布者/侵权方: {{ record.infringer }}

-------------- 原创视频信息 --------------
原创视频ID: {{ record.original_video_id }}
原创视频标题: {{ record.original_video_title }}
版权持有人: {{ record.original_copyright_holder }}

-------------- 匹配详情 --------------
相似度: {{ "%.2f%%" | format(record.similarity * 100) }}
侵权片段起始时间: {{ "%.2f" | format(record.start_timestamp) }} 秒
侵权片段结束时间: {{ "%.2f" | format(record.end_timestamp) }} 秒
侵权片段时长: {{ "%.2f" | format(record.end_timestamp - record.start_timestamp) }} 秒

-------------- 取证材料 --------------
截图证据:
{% for screenshot in record.evidence_screenshots %}
  - {{ screenshot }}
{% endfor %}

本报告URL文件: {{ record.report_path }}

-------------- 状态信息 --------------
当前状态: {{ record.status }}
关联案件ID: {{ record.case_id if record.case_id else '未关联' }}
备注: {{ record.notes if record.notes else '无' }}

=============================================
              报告生成完毕
=============================================
        """)

    def generate_text_report(self, record: InfringementRecord) -> str:
        report_content = self.report_template.render(
            record=record,
            report_time=datetime.now().isoformat()
        )
        
        report_filename = f"infringement_report_{record.id}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"
        report_path = os.path.join(self.report_dir, report_filename)
        
        with open(report_path, 'w', encoding='utf-8') as f:
            f.write(report_content)
        
        logger.info(f"侵权报告已生成: {report_path}")
        return report_path

    def generate_json_report(self, record: InfringementRecord) -> str:
        report_data = {
            "report_id": record.id,
            "report_time": datetime.now().isoformat(),
            "infringement": record.to_dict()
        }
        
        report_filename = f"infringement_report_{record.id}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        report_path = os.path.join(self.report_dir, report_filename)
        
        with open(report_path, 'w', encoding='utf-8') as f:
            json.dump(report_data, f, ensure_ascii=False, indent=2)
        
        logger.info(f"JSON侵权报告已生成: {report_path}")
        return report_path


class WebhookNotifier:
    def __init__(self):
        self.config = webhook_config

    def _generate_signature(self, payload: str) -> str:
        if not self.config.secret:
            return ""
        return hmac.new(
            self.config.secret.encode(),
            payload.encode(),
            hashlib.sha256
        ).hexdigest()

    async def send_infringement_alert(self, record: InfringementRecord) -> bool:
        if not self.config.enabled or not self.config.url:
            logger.warning("Webhook未启用或未配置URL")
            return False

        payload = {
            "event_type": "infringement_detected",
            "timestamp": datetime.now().isoformat(),
            "data": record.to_dict()
        }

        payload_str = json.dumps(payload, ensure_ascii=False)
        signature = self._generate_signature(payload_str)

        headers = {
            "Content-Type": "application/json",
            "X-Signature": signature
        }

        for attempt in range(self.config.retry_count):
            try:
                async with httpx.AsyncClient(timeout=self.config.timeout) as client:
                    response = await client.post(
                        self.config.url,
                        json=payload,
                        headers=headers
                    )
                    
                    if response.status_code == 200:
                        logger.info(f"Webhook通知发送成功 (尝试 {attempt + 1})")
                        return True
                    else:
                        logger.warning(f"Webhook通知失败，状态码: {response.status_code}")
                        
            except Exception as e:
                logger.error(f"Webhook通知异常 (尝试 {attempt + 1}): {e}")
            
            if attempt < self.config.retry_count - 1:
                await asyncio.sleep(2 ** attempt)

        logger.error(f"Webhook通知失败，已重试 {self.config.retry_count} 次")
        return False

    async def send_case_created(self, case_id: str, record: InfringementRecord) -> bool:
        if not self.config.enabled:
            return False

        payload = {
            "event_type": "case_created",
            "timestamp": datetime.now().isoformat(),
            "data": {
                "case_id": case_id,
                "infringement": record.to_dict()
            }
        }

        return await self._send_webhook(payload)

    async def _send_webhook(self, payload: Dict) -> bool:
        payload_str = json.dumps(payload, ensure_ascii=False)
        signature = self._generate_signature(payload_str)

        headers = {
            "Content-Type": "application/json",
            "X-Signature": signature
        }

        try:
            async with httpx.AsyncClient(timeout=self.config.timeout) as client:
                response = await client.post(
                    self.config.url,
                    json=payload,
                    headers=headers
                )
                return response.status_code == 200
        except Exception as e:
            logger.error(f"Webhook发送失败: {e}")
            return False


class VideoBlocker:
    def __init__(self):
        self.config = block_api_config

    async def block_video(self, video_url: str, video_id: str, 
                          reason: str = "版权侵权") -> Dict[str, Any]:
        if not self.config.enabled:
            return {"success": False, "message": "下架API未启用"}

        if not self.config.api_key or not self.config.api_endpoint:
            return {"success": False, "message": "下架API配置不完整"}

        payload = {
            "video_id": video_id,
            "video_url": video_url,
            "reason": reason,
            "timestamp": datetime.now().isoformat()
        }

        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.config.api_key}"
        }

        try:
            async with httpx.AsyncClient(timeout=30) as client:
                response = await client.post(
                    self.config.api_endpoint,
                    json=payload,
                    headers=headers
                )

                if response.status_code == 200:
                    result = response.json()
                    return {"success": True, "data": result}
                else:
                    return {
                        "success": False,
                        "message": f"API返回错误: {response.status_code}",
                        "details": response.text
                    }

        except Exception as e:
            logger.error(f"调用下架API失败: {e}")
            return {"success": False, "message": str(e)}

    async def auto_block_if_needed(self, record: InfringementRecord) -> Dict[str, Any]:
        if not self.config.auto_block:
            return {"success": False, "message": "自动下架未启用"}

        if record.similarity < self.config.min_confidence:
            return {
                "success": False,
                "message": f"相似度 {record.similarity:.2f} 低于阈值 {self.config.min_confidence}"
            }

        logger.info(f"自动下架侵权视频: {record.video_id}")
        return await self.block_video(
            record.video_url,
            record.video_id,
            reason=f"版权侵权，相似度 {record.similarity:.2%}"
        )


class AutoRightsManager:
    def __init__(self):
        self.evidence_collector = EvidenceCollector()
        self.report_generator = InfringementReportGenerator()
        self.webhook_notifier = WebhookNotifier()
        self.video_blocker = VideoBlocker()
        self.infringement_records: Dict[str, InfringementRecord] = {}

    async def process_infringement(self, match_result: Dict, 
                                    video_info: Dict, 
                                    original_info: Dict,
                                    platform: str) -> InfringementRecord:
        record_id = str(uuid.uuid4())
        
        record = InfringementRecord(
            id=record_id,
            video_id=video_info.get("video_id", ""),
            video_title=video_info.get("title", ""),
            video_url=video_info.get("url", ""),
            platform=platform,
            infringer=video_info.get("author", ""),
            similarity=match_result.get("confidence", 0),
            original_video_id=original_info.get("video_id", ""),
            original_video_title=original_info.get("title", ""),
            original_copyright_holder=original_info.get("copyright_holder", ""),
            start_timestamp=match_result.get("start_timestamp", 0),
            end_timestamp=match_result.get("end_timestamp", 0),
            status="detected"
        )

        logger.info(f"开始处理侵权记录: {record_id}")

        try:
            screenshots = self.evidence_collector.capture_multiple_screenshots(
                record.video_url,
                record.video_id,
                [record.start_timestamp, 
                 (record.start_timestamp + record.end_timestamp) / 2,
                 record.end_timestamp]
            )
            record.evidence_screenshots = screenshots

            url_proof = self.evidence_collector.save_page_screenshot_url(
                record.video_url,
                record.video_id
            )
            record.evidence_screenshots.append(url_proof)

        except Exception as e:
            logger.error(f"取证失败: {e}")
            record.notes = f"取证部分失败: {e}"

        try:
            report_path = self.report_generator.generate_text_report(record)
            record.report_path = report_path
            
            self.report_generator.generate_json_report(record)
            
        except Exception as e:
            logger.error(f"生成报告失败: {e}")

        try:
            webhook_success = await self.webhook_notifier.send_infringement_alert(record)
            if webhook_success:
                record.status = "notified"
                
        except Exception as e:
            logger.error(f"发送Webhook失败: {e}")

        try:
            block_result = await self.video_blocker.auto_block_if_needed(record)
            if block_result.get("success"):
                record.status = "blocked"
                record.notes = (record.notes or "") + " | 已自动下架"
                
        except Exception as e:
            logger.error(f"自动下架失败: {e}")

        self.infringement_records[record_id] = record
        logger.info(f"侵权处理完成: {record_id}, 状态: {record.status}")

        return record

    def get_infringement_records(self, status: Optional[str] = None, 
                                 platform: Optional[str] = None,
                                 limit: int = 100) -> List[InfringementRecord]:
        records = list(self.infringement_records.values())
        
        if status:
            records = [r for r in records if r.status == status]
        if platform:
            records = [r for r in records if r.platform == platform]
            
        records.sort(key=lambda r: r.detected_at, reverse=True)
        return records[:limit]

    def get_record_by_id(self, record_id: str) -> Optional[InfringementRecord]:
        return self.infringement_records.get(record_id)

    def update_record_status(self, record_id: str, status: str, 
                             notes: Optional[str] = None) -> bool:
        record = self.infringement_records.get(record_id)
        if not record:
            return False
            
        record.status = status
        if notes:
            record.notes = notes
        return True


import asyncio
