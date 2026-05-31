package com.cdc.core.alert;

import com.cdc.common.config.DdlConfig;
import com.cdc.common.exception.CdcException;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.Base64;
import java.util.HashMap;
import java.util.Map;

public class DingTalkAlertService {

    private static final Logger logger = LoggerFactory.getLogger(DingTalkAlertService.class);

    private final String webhookUrl;
    private final String secret;
    private final String[] atMobiles;
    private final boolean atAll;
    private final HttpClient httpClient;
    private final ObjectMapper objectMapper;

    public DingTalkAlertService(DdlConfig config) {
        if (config.getAlert() == null || config.getAlert().getDingtalk() == null) {
            this.webhookUrl = null;
            this.secret = null;
            this.atMobiles = new String[0];
            this.atAll = false;
            this.httpClient = null;
            this.objectMapper = null;
            return;
        }

        DdlConfig.DingTalkConfig dingtalk = config.getAlert().getDingtalk();
        this.webhookUrl = dingtalk.getWebhookUrl();
        this.secret = dingtalk.getSecret();
        this.atMobiles = dingtalk.getAtMobiles();
        this.atAll = dingtalk.isAtAll();
        this.httpClient = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(10))
                .build();
        this.objectMapper = new ObjectMapper();
    }

    public boolean isEnabled() {
        return webhookUrl != null && !webhookUrl.isEmpty();
    }

    public void sendAlert(String title, String message) {
        if (!isEnabled()) {
            logger.debug("DingTalk alert is not configured");
            return;
        }

        try {
            String url = buildUrl();
            String body = buildRequestBody(title, message);

            HttpRequest request = HttpRequest.newBuilder()
                    .uri(URI.create(url))
                    .header("Content-Type", "application/json")
                    .timeout(Duration.ofSeconds(30))
                    .POST(HttpRequest.BodyPublishers.ofString(body, StandardCharsets.UTF_8))
                    .build();

            HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());

            if (response.statusCode() == 200) {
                logger.info("DingTalk alert sent successfully");
            } else {
                logger.error("Failed to send DingTalk alert: status={}, body={}",
                        response.statusCode(), response.body());
            }
        } catch (Exception e) {
            logger.error("Failed to send DingTalk alert", e);
            throw new CdcException("Failed to send DingTalk alert", e);
        }
    }

    private String buildUrl() {
        if (secret == null || secret.isEmpty()) {
            return webhookUrl;
        }

        long timestamp = System.currentTimeMillis();
        String stringToSign = timestamp + "\n" + secret;

        try {
            Mac mac = Mac.getInstance("HmacSHA256");
            SecretKeySpec keySpec = new SecretKeySpec(secret.getBytes(StandardCharsets.UTF_8), "HmacSHA256");
            mac.init(keySpec);
            byte[] signData = mac.doFinal(stringToSign.getBytes(StandardCharsets.UTF_8));
            String sign = Base64.getEncoder().encodeToString(signData);

            String url = webhookUrl;
            char separator = url.contains("?") ? '&' : '?';
            return String.format("%s%ctimestamp=%d&sign=%s", url, separator, timestamp, sign);
        } catch (Exception e) {
            logger.error("Failed to generate DingTalk signature", e);
            return webhookUrl;
        }
    }

    private String buildRequestBody(String title, String message) {
        try {
            Map<String, Object> requestBody = new HashMap<>();
            requestBody.put("msgtype", "markdown");

            Map<String, Object> markdown = new HashMap<>();
            markdown.put("title", title);
            markdown.put("text", String.format("## %s\n\n```\n%s\n```\n", title, message));
            requestBody.put("markdown", markdown);

            Map<String, Object> at = new HashMap<>();
            at.put("atMobiles", atMobiles != null ? atMobiles : new String[0]);
            at.put("isAtAll", atAll);
            requestBody.put("at", at);

            return objectMapper.writeValueAsString(requestBody);
        } catch (Exception e) {
            logger.error("Failed to build request body", e);
            return "{\"msgtype\":\"text\",\"text\":{\"content\":\"" + title + ": " + message + "\"}}";
        }
    }

    public void sendTestAlert() {
        sendAlert("CDC Sync Test Alert", "This is a test alert from CDC Sync. DingTalk integration is working!");
    }
}
