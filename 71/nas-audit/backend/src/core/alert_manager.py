import logging
import json
import smtplib
import time
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from typing import List, Optional, Dict, Any

from .dlp_config import AlertConfig, SensitiveConfig
from .event_models import FileOperationEvent
from .sensitive_matcher import MatchResult

logger = logging.getLogger(__name__)


class AlertManager:
    def __init__(self, alert_cfg: AlertConfig, sensitive_cfg: SensitiveConfig):
        self.alert_cfg = alert_cfg
        self.sensitive_cfg = sensitive_cfg
        self._alert_timestamps: Dict[str, float] = {}
        self._smtp_server = None

    def _is_cooldown(self, key: str) -> bool:
        now = time.time()
        cooldown = self.sensitive_cfg.alert_cooldown_seconds
        if key in self._alert_timestamps:
            if now - self._alert_timestamps[key] < cooldown:
                return True
        self._alert_timestamps[key] = now
        return False

    def _build_alert_content(
        self,
        event: FileOperationEvent,
        matches: List[MatchResult],
    ) -> Dict[str, Any]:
        severities = sorted(set(m.severity for m in matches))
        categories = sorted(set(m.category for m in matches))
        matched_words = sorted(set(m.word for m in matches))
        top_match = max(matches, key=lambda m: ({"critical": 3, "high": 2, "medium": 1, "low": 0}.get(m.severity, 0), m.confidence), default=None)

        snippet = top_match.snippet if top_match else ""
        now = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime())

        return {
            "event": {
                "operation_type": event.operation_type.value,
                "file_path": event.file_path,
                "old_file_path": event.old_file_path,
                "timestamp": event.timestamp,
                "iso_timestamp": time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(event.timestamp)),
                "username": event.username or "unknown",
                "source_ip": event.source_ip or "unknown",
                "file_size": event.file_size,
                "file_extension": event.file_extension,
            },
            "alert": {
                "severity": severities[0] if severities else "high",
                "categories": categories,
                "matched_words": matched_words,
                "match_count": len(matches),
                "snippet": snippet,
                "detected_at": now,
            },
        }

    def _format_wecom_markdown(self, content: Dict[str, Any]) -> str:
        ev = content["event"]
        al = content["alert"]
        severity_emoji = {"critical": "🔴", "high": "🟠", "medium": "🟡", "low": "🔵"}.get(al["severity"], "⚪")
        lines = [
            f"{severity_emoji} **NAS敏感文件泄露告警**",
            f"> **严重等级**: <font color=\"warning\">{al['severity'].upper()}</font>",
            f"> **操作类型**: {ev['operation_type']}",
            f"> **文件路径**: <font color=\"comment\">{ev['file_path']}</font>",
            f"> **操作用户**: {ev['username']}",
            f"> **源IP**: {ev['source_ip']}",
            f"> **文件大小**: {ev['file_size']:,} bytes",
            f"> **文件类型**: {ev['file_extension']}",
            f"> **操作时间**: {ev['iso_timestamp']}",
            "",
            f"**命中敏感词** ({al['match_count']}个):",
        ]
        for w in al["matched_words"][:10]:
            lines.append(f"  • {w}")
        lines.extend([
            "",
            f"**敏感片段**:",
            f"```\n{al['snippet'][:300]}\n```",
            "",
            f"<font color=\"comment\">检测时间: {al['detected_at']}</font>",
        ])
        return "\n".join(lines)

    def _send_wecom(self, content: Dict[str, Any]) -> bool:
        if not self.alert_cfg.wecom_webhook:
            return False
        try:
            import requests as req
            markdown = self._format_wecom_markdown(content)
            payload = {
                "msgtype": "markdown",
                "markdown": {
                    "content": markdown,
                    "mentioned_mobile_list": self.alert_cfg.wecom_mentioned_mobiles,
                },
            }
            resp = req.post(
                self.alert_cfg.wecom_webhook,
                json=payload,
                timeout=10,
            )
            result = resp.json()
            if result.get("errcode") == 0:
                logger.info("WeCom alert sent successfully")
                return True
            else:
                logger.error(f"WeCom alert failed: {result}")
                return False
        except Exception as e:
            logger.error(f"WeCom alert error: {e}")
            return False

    def _format_email_html(self, content: Dict[str, Any]) -> str:
        ev = content["event"]
        al = content["alert"]
        severity_color = {"critical": "#e74c3c", "high": "#f39c12", "medium": "#f1c40f", "low": "#3498db"}.get(al["severity"], "#95a5a6")

        words_html = "".join(
            f'<span style="display:inline-block;background:#f8f9fa;padding:2px 8px;margin:2px;border-radius:4px;border:1px solid #dee2e6;font-size:12px;">{w}</span>'
            for w in al["matched_words"][:20]
        )

        html = f"""
        <div style="font-family:Arial,Helvetica,sans-serif;max-width:700px;margin:0 auto;padding:20px;border:1px solid #e0e0e0;border-radius:8px;">
            <div style="background:{severity_color};color:white;padding:12px 20px;border-radius:6px 6px 0 0;font-size:18px;font-weight:bold;">
                🔒 NAS敏感文件泄露告警 - {al['severity'].upper()}
            </div>
            <table style="width:100%;border-collapse:collapse;margin-top:16px;">
                <tr><td style="padding:8px;border-bottom:1px solid #eee;width:120px;color:#666;">操作类型</td><td style="padding:8px;border-bottom:1px solid #eee;font-weight:bold;">{ev['operation_type']}</td></tr>
                <tr><td style="padding:8px;border-bottom:1px solid #eee;color:#666;">文件路径</td><td style="padding:8px;border-bottom:1px solid #eee;font-family:monospace;font-size:12px;background:#f8f9fa;">{ev['file_path']}</td></tr>
                <tr><td style="padding:8px;border-bottom:1px solid #eee;color:#666;">操作用户</td><td style="padding:8px;border-bottom:1px solid #eee;">{ev['username']}</td></tr>
                <tr><td style="padding:8px;border-bottom:1px solid #eee;color:#666;">源IP地址</td><td style="padding:8px;border-bottom:1px solid #eee;font-family:monospace;">{ev['source_ip']}</td></tr>
                <tr><td style="padding:8px;border-bottom:1px solid #eee;color:#666;">文件大小</td><td style="padding:8px;border-bottom:1px solid #eee;">{ev['file_size']:,} bytes</td></tr>
                <tr><td style="padding:8px;border-bottom:1px solid #eee;color:#666;">文件类型</td><td style="padding:8px;border-bottom:1px solid #eee;">{ev['file_extension']}</td></tr>
                <tr><td style="padding:8px;border-bottom:1px solid #eee;color:#666;">操作时间</td><td style="padding:8px;border-bottom:1px solid #eee;font-family:monospace;">{ev['iso_timestamp']}</td></tr>
            </table>
            <div style="margin-top:16px;">
                <div style="color:#666;margin-bottom:8px;font-size:13px;">命中敏感词 ({al['match_count']}个):</div>
                <div>{words_html}</div>
            </div>
            <div style="margin-top:16px;background:#f8f9fa;padding:12px;border-radius:6px;border-left:3px solid {severity_color};">
                <div style="color:#666;margin-bottom:4px;font-size:13px;">敏感片段:</div>
                <pre style="margin:0;white-space:pre-wrap;word-break:break-word;font-size:12px;color:#333;max-height:200px;overflow:auto;">{al['snippet'][:500]}</pre>
            </div>
            <div style="margin-top:16px;color:#999;font-size:12px;text-align:center;">
                NAS Audit Service - 检测时间: {al['detected_at']}
            </div>
        </div>
        """
        return html

    def _send_email(self, content: Dict[str, Any]) -> bool:
        if not self.alert_cfg.smtp_host or not self.alert_cfg.smtp_to:
            return False
        try:
            msg = MIMEMultipart("alternative")
            msg["Subject"] = f"[NAS告警] 敏感文件泄露 - {content['alert']['severity'].upper()} - {content['event']['file_path']}"
            msg["From"] = self.alert_cfg.smtp_from or self.alert_cfg.smtp_username
            msg["To"] = ", ".join(self.alert_cfg.smtp_to)

            html = self._format_email_html(content)
            msg.attach(MIMEText(html, "html", "utf-8"))

            if self.alert_cfg.smtp_use_ssl:
                server = smtplib.SMTP_SSL(self.alert_cfg.smtp_host, self.alert_cfg.smtp_port, timeout=30)
            else:
                server = smtplib.SMTP(self.alert_cfg.smtp_host, self.alert_cfg.smtp_port, timeout=30)
                server.starttls()

            if self.alert_cfg.smtp_username and self.alert_cfg.smtp_password:
                server.login(self.alert_cfg.smtp_username, self.alert_cfg.smtp_password)

            server.sendmail(
                self.alert_cfg.smtp_from or self.alert_cfg.smtp_username,
                self.alert_cfg.smtp_to,
                msg.as_string(),
            )
            server.quit()
            logger.info(f"Email alert sent to {self.alert_cfg.smtp_to}")
            return True
        except Exception as e:
            logger.error(f"Email alert error: {e}")
            return False

    def send_alert(
        self,
        event: FileOperationEvent,
        matches: List[MatchResult],
        force: bool = False,
    ) -> Dict[str, bool]:
        if not matches:
            return {"wecom": False, "email": False}

        severities = set(m.severity for m in matches)
        if not force and not any(s in self.alert_cfg.alert_on_severity for s in severities):
            return {"wecom": False, "email": False}

        key = f"{event.file_path}|{event.operation_type.value}"
        if not force and self._is_cooldown(key):
            logger.debug(f"Alert suppressed (cooldown): {key}")
            return {"wecom": False, "email": False}

        content = self._build_alert_content(event, matches)

        wecom_ok = False
        email_ok = False

        if self.alert_cfg.wecom_webhook:
            wecom_ok = self._send_wecom(content)
        if self.alert_cfg.smtp_host and self.alert_cfg.smtp_to:
            email_ok = self._send_email(content)

        return {"wecom": wecom_ok, "email": email_ok}

    def send_test_alert(self) -> Dict[str, bool]:
        from .event_models import OperationType
        event = FileOperationEvent(
            operation_type=OperationType.CREATE,
            file_path="/test/sensitive.docx",
            timestamp=time.time(),
            username="testuser",
            source_ip="192.168.1.1",
            file_size=1024,
            file_extension=".docx",
        )
        match = MatchResult(
            word="机密",
            category="default",
            severity="high",
            description="test",
            snippet="这是一个**机密**测试文档...",
            position=0,
            regex=False,
            confidence=1.0,
        )
        return self.send_alert(event, [match], force=True)
