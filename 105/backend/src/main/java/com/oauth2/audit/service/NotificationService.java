package com.oauth2.audit.service;

import com.oauth2.audit.config.MLConfig;
import com.oauth2.audit.entity.RiskEvent;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

@Service
@RequiredArgsConstructor
@Slf4j
public class NotificationService {
    private final MLConfig mlConfig;

    public void sendAnomalyAlert(RiskEvent riskEvent) {
        if (mlConfig.getNotification().getEmail().isEnabled()) {
            sendEmailAlert(riskEvent);
        }

        if (mlConfig.getNotification().getSms().isEnabled()) {
            sendSmsAlert(riskEvent);
        }

        log.info("Anomaly alert sent for risk event {}: level={}, user={}",
                riskEvent.getId(), riskEvent.getRiskLevel(),
                riskEvent.getUser() != null ? riskEvent.getUser().getUsername() : "unknown");
    }

    private void sendEmailAlert(RiskEvent riskEvent) {
        String subject = buildEmailSubject(riskEvent);
        String body = buildEmailBody(riskEvent);

        MLConfig.Email emailConfig = mlConfig.getNotification().getEmail();
        log.info("Sending email alert via SMTP {}:{} from {}",
                emailConfig.getSmtpHost(), emailConfig.getSmtpPort(), emailConfig.getFromAddress());

        try {
            log.info("Email would be sent to: {}", riskEvent.getNotifyEmail());
            log.debug("Email subject: {}", subject);
            log.debug("Email body: {}", body);
        } catch (Exception e) {
            log.error("Failed to send email alert: {}", e.getMessage());
        }
    }

    private void sendSmsAlert(RiskEvent riskEvent) {
        String message = buildSmsMessage(riskEvent);

        MLConfig.Sms smsConfig = mlConfig.getNotification().getSms();
        log.info("Sending SMS alert via {} from {}",
                smsConfig.getProvider(), smsConfig.getFromNumber());

        try {
            log.info("SMS would be sent to: {}", riskEvent.getNotifyPhone());
            log.debug("SMS message: {}", message);
        } catch (Exception e) {
            log.error("Failed to send SMS alert: {}", e.getMessage());
        }
    }

    private String buildEmailSubject(RiskEvent riskEvent) {
        String levelStr = switch (riskEvent.getRiskLevel()) {
            case CRITICAL -> "紧急";
            case HIGH -> "高危";
            case MEDIUM -> "中等";
            case LOW -> "低危";
        };

        String username = riskEvent.getUser() != null ? riskEvent.getUser().getUsername() : "Unknown";

        return String.format("【安全告警】检测到%s风险授权 - 用户%s", levelStr, username);
    }

    private String buildEmailBody(RiskEvent riskEvent) {
        StringBuilder body = new StringBuilder();
        body.append("您好，\n\n");
        body.append("我们检测到您的账户存在一项异常授权行为，请确认是否为本人操作：\n\n");

        if (riskEvent.getAuthorization() != null) {
            body.append(String.format("应用名称：%s\n",
                riskEvent.getAuthorization().getClientApplication() != null ?
                    riskEvent.getAuthorization().getClientApplication().getClientName() : "Unknown"));
            body.append(String.format("授权时间：%s\n", riskEvent.getDetectedAt()));
            body.append(String.format("IP地址：%s\n",
                riskEvent.getAuthorization().getIpAddress() != null ?
                    riskEvent.getAuthorization().getIpAddress() : "Unknown"));
            body.append(String.format("设备：%s\n",
                riskEvent.getAuthorization().getDeviceName() != null ?
                    riskEvent.getAuthorization().getDeviceName() : "Unknown"));
        }

        body.append(String.format("\n异常评分：%.2f\n", riskEvent.getAnomalyScore()));
        body.append(String.format("风险等级：%s\n", riskEvent.getRiskLevel()));
        body.append(String.format("风险类型：%s\n", riskEvent.getRiskType().getDisplayName()));
        body.append(String.format("风险原因：%s\n", riskEvent.getRiskReason()));

        body.append("\n\n如果这不是您本人操作，请立即点击以下链接撤销授权并修改密码：\n");
        body.append(String.format("/risk-events/%d/revoke\n\n", riskEvent.getId()));

        body.append("如果您确认这是本人操作，可以忽略此邮件。\n\n");
        body.append("此致\n");
        body.append("OAuth2 权限审计系统\n");

        return body.toString();
    }

    private String buildSmsMessage(RiskEvent riskEvent) {
        String levelStr = switch (riskEvent.getRiskLevel()) {
            case CRITICAL -> "紧急";
            case HIGH -> "高危";
            case MEDIUM -> "中等";
            case LOW -> "低危";
        };

        return String.format(
            "【OAuth2安全告警】检测到%s风险授权行为，异常评分%.2f。请立即登录系统确认。如非本人操作请尽快撤销。",
            levelStr, riskEvent.getAnomalyScore()
        );
    }

    public void sendConfirmationReminder(RiskEvent riskEvent) {
        if (mlConfig.getNotification().getEmail().isEnabled()) {
            log.info("Sending confirmation reminder for risk event {} to {}",
                    riskEvent.getId(), riskEvent.getNotifyEmail());
        }
    }
}
