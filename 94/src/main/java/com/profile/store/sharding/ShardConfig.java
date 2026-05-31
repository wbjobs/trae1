package com.profile.store.sharding;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

/**
 * 分片配置：从 application.yml 读取分片相关配置。
 */
@Configuration
public class ShardConfig {

    @Bean
    @ConfigurationProperties(prefix = "profile.sharding")
    public ShardProperties shardProperties() {
        return new ShardProperties();
    }

    @Bean
    public ShardRouter shardRouter(ShardProperties properties) {
        return new ShardRouter(properties);
    }
}
