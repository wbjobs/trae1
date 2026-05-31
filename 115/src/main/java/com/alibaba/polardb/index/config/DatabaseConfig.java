package com.alibaba.polardb.index.config;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.core.namedparam.NamedParameterJdbcTemplate;

import javax.sql.DataSource;

@Configuration
public class DatabaseConfig {

    @Autowired
    private GlobalIndexSyncProperties properties;

    @Bean(name = "centerDataSource")
    public DataSource centerDataSource() {
        CenterDbConfig config = properties.getCenterDb();
        HikariConfig hikariConfig = new HikariConfig();
        hikariConfig.setJdbcUrl(config.getUrl());
        hikariConfig.setUsername(config.getUsername());
        hikariConfig.setPassword(config.getPassword());
        hikariConfig.setDriverClassName(config.getDriverClassName());
        hikariConfig.setMaximumPoolSize(config.getMaximumPoolSize());
        hikariConfig.setMinimumIdle(config.getMinimumIdle());
        hikariConfig.setConnectionTimeout(config.getConnectionTimeout());
        hikariConfig.setIdleTimeout(config.getIdleTimeout());
        hikariConfig.setMaxLifetime(config.getMaxLifetime());
        hikariConfig.setAutoCommit(true);
        hikariConfig.addDataSourceProperty("cachePrepStmts", "true");
        hikariConfig.addDataSourceProperty("prepStmtCacheSize", "250");
        hikariConfig.addDataSourceProperty("prepStmtCacheSqlLimit", "2048");
        hikariConfig.addDataSourceProperty("useServerPrepStmts", "true");
        hikariConfig.addDataSourceProperty("rewriteBatchedStatements", "true");
        return new HikariDataSource(hikariConfig);
    }

    @Bean(name = "centerJdbcTemplate")
    public JdbcTemplate centerJdbcTemplate(DataSource centerDataSource) {
        return new JdbcTemplate(centerDataSource);
    }

    @Bean(name = "centerNamedParameterJdbcTemplate")
    public NamedParameterJdbcTemplate centerNamedParameterJdbcTemplate(DataSource centerDataSource) {
        return new NamedParameterJdbcTemplate(centerDataSource);
    }
}
