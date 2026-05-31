package com.configcenter.server.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ClientStateEntity {
    private String instanceId;
    private String clientIp;
    private String application;
    private String profile;
    private long version;
    private boolean healthy;
    private String errorMessage;
    private long lastSeenAt;
}
