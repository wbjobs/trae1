package com.rsocket.gateway.scripting.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.Instant;
import java.util.HashMap;
import java.util.Map;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class InterceptContext {

    private String interactionType;
    private String route;
    private Object payload;
    private Map<String, Object> metadata;
    private String clientId;
    private String connectionId;
    private long requestN;
    private Object response;
    private Throwable error;
    private Instant timestamp;
    private final Map<String, Object> attributes = new HashMap<>();
    private boolean modified;
    private boolean rejected;
    private String rejectionReason;

    public void setAttribute(String key, Object value) {
        attributes.put(key, value);
    }

    @SuppressWarnings("unchecked")
    public <T> T getAttribute(String key) {
        return (T) attributes.get(key);
    }

    public void reject(String reason) {
        this.rejected = true;
        this.rejectionReason = reason;
    }

    public void modifyPayload(Object newPayload) {
        this.payload = newPayload;
        this.modified = true;
    }

    public void addMetadata(String key, Object value) {
        if (metadata == null) {
            metadata = new HashMap<>();
        }
        metadata.put(key, value);
        this.modified = true;
    }

    public void modifyResponse(Object newResponse) {
        this.response = newResponse;
        this.modified = true;
    }
}
