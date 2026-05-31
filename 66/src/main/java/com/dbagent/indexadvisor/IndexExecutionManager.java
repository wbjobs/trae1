package com.dbagent.indexadvisor;

import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Iterator;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public class IndexExecutionManager {

    private static volatile IndexExecutionManager instance;
    private static final Object LOCK = new Object();

    private final Map<String, IndexSuggestion> pendingSuggestions = new ConcurrentHashMap<>();
    private final Map<String, IndexSuggestion> executedSuggestions = new ConcurrentHashMap<>();
    private final Map<String, IndexSuggestion> rejectedSuggestions = new ConcurrentHashMap<>();

    private WebhookNotifier webhookNotifier;
    private boolean autoExecute;
    private long pendingTimeoutMs = 86400000;

    private ScheduledExecutorService cleanupExecutor;

    private IndexExecutionManager() {
    }

    public static IndexExecutionManager getInstance() {
        if (instance == null) {
            synchronized (LOCK) {
                if (instance == null) {
                    instance = new IndexExecutionManager();
                }
            }
        }
        return instance;
    }

    public void initialize(WebhookNotifier notifier, boolean autoExecute, long pendingTimeoutMs) {
        this.webhookNotifier = notifier;
        this.autoExecute = autoExecute;
        this.pendingTimeoutMs = pendingTimeoutMs > 0 ? pendingTimeoutMs : 86400000;

        cleanupExecutor = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "index-executor-cleanup");
            t.setDaemon(true);
            return t;
        });
        cleanupExecutor.scheduleAtFixedRate(this::cleanup,
                3600, 3600, TimeUnit.SECONDS);

        System.out.println("[DB-Tracing-Agent] IndexExecutionManager initialized, autoExecute: "
                + autoExecute);
    }

    public void submitSuggestion(IndexSuggestion suggestion, Connection connection) {
        if (suggestion == null) {
            return;
        }

        String token = generateToken();
        suggestion.setApprovalToken(token);

        pendingSuggestions.put(suggestion.getId(), suggestion);

        if (webhookNotifier != null && webhookNotifier.isEnabled()) {
            webhookNotifier.notify(suggestion);
        }

        System.out.println("[DB-Tracing-Agent] Index suggestion submitted: " + suggestion.getId()
                + ", token: " + token);

        if (autoExecute && connection != null) {
            System.out.println("[DB-Tracing-Agent] Auto-executing index: " + suggestion.getIndexName());
            executeIndex(suggestion.getId(), token, connection);
        }
    }

    public boolean executeIndex(String suggestionId, String token, Connection connection) {
        IndexSuggestion suggestion = pendingSuggestions.get(suggestionId);
        if (suggestion == null) {
            System.err.println("[DB-Tracing-Agent] Suggestion not found: " + suggestionId);
            return false;
        }

        if (token == null || !token.equals(suggestion.getApprovalToken())) {
            System.err.println("[DB-Tracing-Agent] Invalid approval token for: " + suggestionId);
            return false;
        }

        if (suggestion.getStatus() != IndexSuggestion.SuggestionStatus.PENDING) {
            System.err.println("[DB-Tracing-Agent] Suggestion not in PENDING status: "
                    + suggestion.getStatus());
            return false;
        }

        try {
            if (indexExists(connection, suggestion)) {
                suggestion.setStatus(IndexSuggestion.SuggestionStatus.EXECUTED);
                suggestion.setReason("Index already exists");
                pendingSuggestions.remove(suggestionId);
                executedSuggestions.put(suggestionId, suggestion);
                notifyResult(suggestion, true, "Index already exists");
                return true;
            }

            suggestion.setStatus(IndexSuggestion.SuggestionStatus.APPROVED);

            try (Statement stmt = connection.createStatement()) {
                String createSql = suggestion.getCreateIndexSql();
                System.out.println("[DB-Tracing-Agent] Executing: " + createSql);
                stmt.execute(createSql);
            }

            suggestion.setStatus(IndexSuggestion.SuggestionStatus.EXECUTED);
            pendingSuggestions.remove(suggestionId);
            executedSuggestions.put(suggestionId, suggestion);

            notifyResult(suggestion, true, "Index created successfully");

            System.out.println("[DB-Tracing-Agent] Index created: " + suggestion.getIndexName());
            return true;
        } catch (Exception e) {
            suggestion.setStatus(IndexSuggestion.SuggestionStatus.FAILED);
            suggestion.setRejectionReason(e.getMessage());

            notifyResult(suggestion, false, e.getMessage());

            System.err.println("[DB-Tracing-Agent] Failed to create index: "
                    + suggestion.getIndexName() + ", error: " + e.getMessage());
            return false;
        }
    }

    public boolean rejectSuggestion(String suggestionId, String token, String reason) {
        IndexSuggestion suggestion = pendingSuggestions.get(suggestionId);
        if (suggestion == null) {
            System.err.println("[DB-Tracing-Agent] Suggestion not found: " + suggestionId);
            return false;
        }

        if (token == null || !token.equals(suggestion.getApprovalToken())) {
            System.err.println("[DB-Tracing-Agent] Invalid approval token for: " + suggestionId);
            return false;
        }

        suggestion.setStatus(IndexSuggestion.SuggestionStatus.REJECTED);
        suggestion.setRejectionReason(reason != null ? reason : "Manually rejected");

        pendingSuggestions.remove(suggestionId);
        rejectedSuggestions.put(suggestionId, suggestion);

        notifyResult(suggestion, false, suggestion.getRejectionReason());

        System.out.println("[DB-Tracing-Agent] Suggestion rejected: " + suggestionId);
        return true;
    }

    public IndexSuggestion getSuggestion(String suggestionId) {
        IndexSuggestion suggestion = pendingSuggestions.get(suggestionId);
        if (suggestion != null) {
            return suggestion;
        }
        suggestion = executedSuggestions.get(suggestionId);
        if (suggestion != null) {
            return suggestion;
        }
        return rejectedSuggestions.get(suggestionId);
    }

    public Map<String, IndexSuggestion> getPendingSuggestions() {
        return pendingSuggestions;
    }

    public Map<String, IndexSuggestion> getExecutedSuggestions() {
        return executedSuggestions;
    }

    public Map<String, IndexSuggestion> getRejectedSuggestions() {
        return rejectedSuggestions;
    }

    private boolean indexExists(Connection connection, IndexSuggestion suggestion) {
        try {
            DatabaseMetaData metaData = connection.getMetaData();
            String catalog = connection.getCatalog();
            String schema = null;
            try {
                schema = connection.getSchema();
            } catch (Exception e) {
                // ignore
            }

            ResultSet indexes = metaData.getIndexInfo(catalog, schema,
                    suggestion.getTableName(), false, false);
            while (indexes.next()) {
                String indexName = indexes.getString("INDEX_NAME");
                if (suggestion.getIndexName().equalsIgnoreCase(indexName)) {
                    indexes.close();
                    return true;
                }
            }
            indexes.close();
        } catch (Exception e) {
            // ignore
        }
        return false;
    }

    private void notifyResult(IndexSuggestion suggestion, boolean success, String message) {
        if (webhookNotifier != null && webhookNotifier.isEnabled()) {
            webhookNotifier.notifyExecutionResult(suggestion, success, message);
        }
    }

    private String generateToken() {
        return UUID.randomUUID().toString().replace("-", "").substring(0, 16);
    }

    private void cleanup() {
        long now = System.currentTimeMillis();
        Iterator<Map.Entry<String, IndexSuggestion>> it =
                pendingSuggestions.entrySet().iterator();
        while (it.hasNext()) {
            Map.Entry<String, IndexSuggestion> entry = it.next();
            IndexSuggestion suggestion = entry.getValue();
            if (suggestion.getSuggestionTime() != null) {
                long age = now - suggestion.getSuggestionTime().getTime();
                if (age > pendingTimeoutMs) {
                    suggestion.setStatus(IndexSuggestion.SuggestionStatus.REJECTED);
                    suggestion.setRejectionReason("Timeout after " + (age / 1000) + " seconds");
                    rejectedSuggestions.put(entry.getKey(), suggestion);
                    it.remove();
                    System.out.println("[DB-Tracing-Agent] Suggestion timeout: " + entry.getKey());
                }
            }
        }
    }

    public void shutdown() {
        if (cleanupExecutor != null) {
            cleanupExecutor.shutdown();
            cleanupExecutor = null;
        }
    }

    public boolean isAutoExecute() {
        return autoExecute;
    }

    public void setAutoExecute(boolean autoExecute) {
        this.autoExecute = autoExecute;
    }

    public void setWebhookNotifier(WebhookNotifier webhookNotifier) {
        this.webhookNotifier = webhookNotifier;
    }
}
