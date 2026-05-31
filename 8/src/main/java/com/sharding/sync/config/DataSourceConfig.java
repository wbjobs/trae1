package com.sharding.sync.config;

import com.alibaba.druid.spring.boot.autoconfigure.DruidDataSourceBuilder;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Primary;
import org.springframework.jdbc.core.JdbcTemplate;

import javax.sql.DataSource;
import java.util.HashMap;
import java.util.Map;

@Configuration
public class DataSourceConfig {

    @Bean
    @ConfigurationProperties("spring.datasource.dynamic.datasource.master")
    public DataSource masterDataSource() {
        return DruidDataSourceBuilder.create().build();
    }

    @Bean
    @ConfigurationProperties("spring.datasource.dynamic.datasource.physical0")
    public DataSource physical0DataSource() {
        return DruidDataSourceBuilder.create().build();
    }

    @Bean
    @ConfigurationProperties("spring.datasource.dynamic.datasource.physical1")
    public DataSource physical1DataSource() {
        return DruidDataSourceBuilder.create().build();
    }

    @Bean
    @Primary
    public DynamicDataSource dynamicDataSource(DataSource masterDataSource,
                                               @Qualifier("physical0DataSource") DataSource physical0DataSource,
                                               @Qualifier("physical1DataSource") DataSource physical1DataSource) {
        Map<Object, Object> targetDataSources = new HashMap<>();
        targetDataSources.put("master", masterDataSource);
        targetDataSources.put("physical0", physical0DataSource);
        targetDataSources.put("physical1", physical1DataSource);
        return new DynamicDataSource(masterDataSource, targetDataSources);
    }

    @Bean
    public JdbcTemplate jdbcTemplate(DynamicDataSource dynamicDataSource) {
        return new JdbcTemplate(dynamicDataSource);
    }
}
