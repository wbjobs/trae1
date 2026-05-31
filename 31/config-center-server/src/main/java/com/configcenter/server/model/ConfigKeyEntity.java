package com.configcenter.server.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ConfigKeyEntity {
    private String application;
    private String profile;
    private String key;

    public static ConfigKeyEntity of(String application, String profile, String key) {
        return ConfigKeyEntity.builder()
                .application(application)
                .profile(profile)
                .key(key == null ? "" : key)
                .build();
    }
}
