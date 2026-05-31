package com.dbagent.indexadvisor;

import java.io.BufferedReader;
import java.io.OutputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

public class WebhookNotifier {

    private String webhookUrl;
    private String webhookType;
    private String secret;
    private boolean enabled;

    public WebhookNotifier() {
        this.enabled = false;
    }

    public WebhookNotifier(String webhookUrl, String webhookType) {
        this.webhookUrl = webhookUrl;
        this.webhookType = webhookType;
        this.enabled = (webhookUrl != null && !webhookUrl.isEmpty());
    }

    public WebhookNotifier(String webhookUrl, String webhookType, String secret) {
        this(webhookUrl, webhookType);
        this.secret = secret;
    }

    public void notify(IndexSuggestion suggestion) {
        if (!enabled || suggestion == null) {
            return;
        }

        try {
            String payload = buildPayload(suggestion);
            sendRequest(payload);
            System.out.println("[DB-Tracing-Agent] Index suggestion notification sent: "
                    + suggestion.getId());
        } catch (Exception e) {
            System.err.println("[DB-Tracing-Agent] Failed to send webhook notification: "
                    + e.getMessage());
        }
    }

    public void notifyExecutionResult(IndexSuggestion suggestion, boolean success, String message) {
        if (!enabled || suggestion == null) {
            return;
        }

        try {
            String payload = buildExecutionPayload(suggestion, success, message);
            sendRequest(payload);
            System.out.println("[DB-Tracing-Agent] Index execution result notification sent: "
                    + suggestion.getId());
        } catch (Exception e) {
            System.err.println("[DB-Tracing-Agent] Failed to send execution notification: "
                    + e.getMessage());
        }
    }

    private String buildPayload(IndexSuggestion suggestion) {
        String title = "慢SQL索引建议 [" + suggestion.getPriority() + "]";
        String text = buildMarkdownContent(suggestion);

        if ("dingtalk".equalsIgnoreCase(webhookType)) {
            return buildDingTalkPayload(title, text, suggestion);
        } else if ("feishu".equalsIgnoreCase(webhookType)) {
            return buildFeishuPayload(title, text, suggestion);
        } else {
            return buildGenericPayload(title, text, suggestion);
        }
    }

    private String buildExecutionPayload(IndexSuggestion suggestion, boolean success, String message) {
        String title = "索引执行结果 - " + (success ? "成功" : "失败");
        String text = "**索引名称:** " + suggestion.getIndexName() + "\n" +
                "**执行状态:** " + (success ? "✅ 成功" : "❌ 失败") + "\n" +
                "**详细信息:** " + message;

        if ("dingtalk".equalsIgnoreCase(webhookType)) {
            return buildDingTalkPayload(title, text, suggestion);
        } else if ("feishu".equalsIgnoreCase(webhookType)) {
            return buildFeishuPayload(title, text, suggestion);
        } else {
            return buildGenericPayload(title, text, suggestion);
        }
    }

    private String buildMarkdownContent(IndexSuggestion suggestion) {
        StringBuilder sb = new StringBuilder();
        sb.append("**表名:** ").append(suggestion.getTableName()).append("\n");
        sb.append("**建议索引:** ").append(suggestion.getIndexName()).append("\n");
        sb.append("**索引列:** ").append(String.join(", ", suggestion.getColumns())).append("\n");
        sb.append("**执行耗时:** ").append(suggestion.getExecutionTimeMs()).append("ms\n");
        sb.append("**优先级:** ").append(suggestion.getPriority()).append("\n");

        if (suggestion.getReason() != null) {
            sb.append("**原因:** ").append(suggestion.getReason()).append("\n");
        }

        sb.append("\n**CREATE INDEX语句:**\n```sql\n")
                .append(suggestion.getCreateIndexSql())
                .append("\n```");

        if (suggestion.getOriginalSql() != null) {
            String original = suggestion.getOriginalSql();
            if (original.length() > 500) {
                original = original.substring(0, 500) + "...";
            }
            sb.append("\n**原始SQL:**\n```sql\n").append(original).append("\n```");
        }

        sb.append("\n**审批令牌:** `").append(suggestion.getApprovalToken()).append("`");

        return sb.toString();
    }

    private String buildDingTalkPayload(String title, String text, IndexSuggestion suggestion) {
        StringBuilder sb = new StringBuilder();
        sb.append("{");
        sb.append("\"msgtype\":\"markdown\",");
        sb.append("\"markdown\":{");
        sb.append("\"title\":\"").append(escapeJson(title)).append("\",");
        sb.append("\"text\":\"").append(escapeJson(text)).append("\"");
        sb.append("},");
        sb.append("\"at\":{");
        sb.append("\"isAtAll\":false");
        sb.append("}");
        sb.append("}");
        return sb.toString();
    }

    private String buildFeishuPayload(String title, String text, IndexSuggestion suggestion) {
        StringBuilder sb = new StringBuilder();
        sb.append("{");
        sb.append("\"msg_type\":\"interactive\",");
        sb.append("\"card\":{");
        sb.append("\"header\":{");
        sb.append("\"title\":{");
        sb.append("\"tag\":\"plain_text\",");
        sb.append("\"content\":\"").append(escapeJson(title)).append("\"");
        sb.append("},");
        sb.append("\"template\":\"").append(getPriorityColor(suggestion.getPriority())).append("\"");
        sb.append("},");
        sb.append("\"elements\":[{");
        sb.append("\"tag\":\"markdown\",");
        sb.append("\"content\":\"").append(escapeJson(text)).append("\"");
        sb.append("}]");
        sb.append("}");
        sb.append("}");
        return sb.toString();
    }

    private String buildGenericPayload(String title, String text, IndexSuggestion suggestion) {
        StringBuilder sb = new StringBuilder();
        sb.append("{");
        sb.append("\"title\":\"").append(escapeJson(title)).append("\",");
        sb.append("\"text\":\"").append(escapeJson(text)).append("\",");
        sb.append("\"suggestionId\":\"").append(suggestion.getId()).append("\",");
        sb.append("\"tableName\":\"").append(suggestion.getTableName()).append("\",");
        sb.append("\"indexName\":\"").append(suggestion.getIndexName()).append("\",");
        sb.append("\"columns\":").append(toJsonArray(suggestion.getColumns())).append(",");
        sb.append("\"executionTimeMs\":").append(suggestion.getExecutionTimeMs()).append(",");
        sb.append("\"priority\":\"").append(suggestion.getPriority()).append("\",");
        sb.append("\"createIndexSql\":\"").append(escapeJson(suggestion.getCreateIndexSql())).append("\",");
        sb.append("\"approvalToken\":\"").append(suggestion.getApprovalToken()).append("\"");
        sb.append("}");
        return sb.toString();
    }

    private String getPriorityColor(IndexSuggestion.Priority priority) {
        switch (priority) {
            case HIGH:
                return "red";
            case MEDIUM:
                return "orange";
            default:
                return "blue";
        }
    }

    private String toJsonArray(List<String> list) {
        if (list == null || list.isEmpty()) {
            return "[]";
        }
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < list.size(); i++) {
            if (i > 0) sb.append(",");
            sb.append("\"").append(escapeJson(list.get(i))).append("\"");
        }
        sb.append("]");
        return sb.toString();
    }

    private String escapeJson(String str) {
        if (str == null) {
            return "";
        }
        return str.replace("\\", "\\\\")
                .replace("\"", "\\\"")
                .replace("\n", "\\n")
                .replace("\r", "\\r")
                .replace("\t", "\\t");
    }

    private void sendRequest(String payload) throws Exception {
        URL url = new URL(webhookUrl);
        HttpURLConnection conn = (HttpURLConnection) url.openConnection();
        conn.setRequestMethod("POST");
        conn.setRequestProperty("Content-Type", "application/json; charset=UTF-8");
        conn.setDoOutput(true);
        conn.setConnectTimeout(5000);
        conn.setReadTimeout(10000);

        try (OutputStream os = conn.getOutputStream()) {
            os.write(payload.getBytes(StandardCharsets.UTF_8));
            os.flush();
        }

        int responseCode = conn.getResponseCode();
        if (responseCode != 200) {
            BufferedReader reader = new BufferedReader(
                    new InputStreamReader(conn.getErrorStream(), StandardCharsets.UTF_8));
            StringBuilder response = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                response.append(line);
            }
            reader.close();
            throw new RuntimeException("Webhook request failed: " + responseCode
                    + ", response: " + response);
        }

        conn.disconnect();
    }

    public boolean isEnabled() {
        return enabled;
    }

    public void setEnabled(boolean enabled) {
        this.enabled = enabled;
    }

    public String getWebhookUrl() {
        return webhookUrl;
    }

    public void setWebhookUrl(String webhookUrl) {
        this.webhookUrl = webhookUrl;
        this.enabled = (webhookUrl != null && !webhookUrl.isEmpty());
    }

    public String getWebhookType() {
        return webhookType;
    }

    public void setWebhookType(String webhookType) {
        this.webhookType = webhookType;
    }

    public String getSecret() {
        return secret;
    }

    public void setSecret(String secret) {
        this.secret = secret;
    }
}
