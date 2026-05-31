package com.sharding.sync.config;

import com.alibaba.druid.pool.DruidDataSource;
import com.sharding.sync.common.BusinessException;
import lombok.extern.slf4j.Slf4j;
import org.springframework.jdbc.datasource.lookup.AbstractRoutingDataSource;

import javax.sql.DataSource;
import java.sql.Connection;
import java.sql.DriverManager;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

@Slf4j
public class DynamicDataSource extends AbstractRoutingDataSource {

    private final Map<Object, Object> resolvedDataSources = new ConcurrentHashMap<>();

    public DynamicDataSource(DataSource defaultTargetDataSource, Map<Object, Object> targetDataSources) {
        super.setDefaultTargetDataSource(defaultTargetDataSource);
        super.setTargetDataSources(targetDataSources);
        this.resolvedDataSources.putAll(targetDataSources);
        super.afterPropertiesSet();
    }

    public void addDataSource(String key, String url, String username, String password) {
        if (resolvedDataSources.containsKey(key)) {
            return;
        }
        try {
            DruidDataSource ds = new DruidDataSource();
            ds.setUrl(url);
            ds.setUsername(username);
            ds.setPassword(password);
            ds.setDriverClassName("com.mysql.cj.jdbc.Driver");
            ds.setInitialSize(2);
            ds.setMinIdle(2);
            ds.setMaxActive(16);
            ds.setTestWhileIdle(true);
            ds.setValidationQuery("SELECT 1");
            ds.init();
            resolvedDataSources.put(key, ds);
            super.setTargetDataSources(resolvedDataSources);
            super.afterPropertiesSet();
            log.info("动态数据源已加载: {}", key);
        } catch (Exception e) {
            throw new BusinessException("加载动态数据源失败: " + e.getMessage());
        }
    }

    public boolean hasDataSource(String key) {
        return resolvedDataSources.containsKey(key);
    }

    public Set<Object> getDataSourceKeys() {
        return resolvedDataSources.keySet();
    }

    public DataSource getDataSource(String key) {
        Object ds = resolvedDataSources.get(key);
        if (ds instanceof DataSource) {
            return (DataSource) ds;
        }
        throw new BusinessException("未找到数据源: " + key);
    }

    public Connection getConnection(String key) {
        try {
            return getDataSource(key).getConnection();
        } catch (Exception e) {
            throw new BusinessException("获取连接失败: " + e.getMessage());
        }
    }

    public Connection getConnectionByUrl(String url, String username, String password) {
        try {
            return DriverManager.getConnection(url, username, password);
        } catch (Exception e) {
            throw new BusinessException("获取物理连接失败: " + e.getMessage());
        }
    }

    @Override
    protected Object determineCurrentLookupKey() {
        return DataSourceContextHolder.get();
    }
}
