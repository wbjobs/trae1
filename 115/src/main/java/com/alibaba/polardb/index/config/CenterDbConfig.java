package com.alibaba.polardb.index.config;

import lombok.Data;

@Data
public class CenterDbConfig {
    private String url;
    private String username;
    private String password;
    private String driverClassName;
    private int maximumPoolSize = 64;
    private int minimumIdle = 16;
    private long connectionTimeout = 3000;
    private long idleTimeout = 60000;
    private long maxLifetime = 1800000;
}
