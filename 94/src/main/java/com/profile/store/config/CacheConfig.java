package com.profile.store.config;

import com.github.benmanes.caffeine.cache.Caffeine;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.cache.CacheManager;
import org.springframework.cache.annotation.EnableCaching;
import org.springframework.cache.caffeine.CaffeineCacheManager;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
@EnableCaching
@EnableConfigurationProperties(ProfileProperties.class)
public class CacheConfig {

    @Bean
    public CacheManager cacheManager(ProfileProperties properties) {
        CaffeineCacheManager cacheManager = new CaffeineCacheManager("tagBitmap");
        cacheManager.setCaffeine(Caffeine.newBuilder()
                .maximumSize(properties.getCache().getMaximumSize())
                .expireAfterWrite(properties.getCache().getExpireAfterWrite())
                .recordStats());
        return cacheManager;
    }
}
