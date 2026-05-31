package com.orchestrator.executor;

import com.orchestrator.model.TaskNode;
import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.sql.*;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

public class SqlTaskExecutor {

    private static final Logger logger = LoggerFactory.getLogger(SqlTaskExecutor.class);
    private final Map<String, HikariDataSource> dataSourceCache = new ConcurrentHashMap<>();

    public ExecutionResult execute(TaskNode taskNode) {
        Map<String, Object> params = taskNode.getParams();
        String jdbcUrl = (String) params.get("jdbcUrl");
        String username = (String) params.get("username");
        String password = (String) params.get("password");
        String sql = (String) params.get("sql");
        String driver = (String) params.get("driver");

        if (jdbcUrl == null || jdbcUrl.isEmpty()) {
            return ExecutionResult.failure("JDBC URL is empty");
        }
        if (sql == null || sql.isEmpty()) {
            return ExecutionResult.failure("SQL is empty");
        }

        HikariDataSource dataSource = getOrCreateDataSource(jdbcUrl, username, password, driver);

        StringBuilder output = new StringBuilder();
        try (Connection conn = dataSource.getConnection();
             Statement stmt = conn.createStatement()) {

            String trimmedSql = sql.trim();
            boolean isQuery = trimmedSql.toUpperCase().startsWith("SELECT") ||
                              trimmedSql.toUpperCase().startsWith("SHOW") ||
                              trimmedSql.toUpperCase().startsWith("DESCRIBE") ||
                              trimmedSql.toUpperCase().startsWith("DESC");

            if (isQuery) {
                try (ResultSet rs = stmt.executeQuery(trimmedSql)) {
                    ResultSetMetaData meta = rs.getMetaData();
                    int colCount = meta.getColumnCount();

                    List<String> headers = new ArrayList<>();
                    for (int i = 1; i <= colCount; i++) {
                        headers.add(meta.getColumnLabel(i));
                    }
                    output.append(String.join("\t", headers)).append("\n");

                    int rowCount = 0;
                    while (rs.next()) {
                        List<String> row = new ArrayList<>();
                        for (int i = 1; i <= colCount; i++) {
                            Object val = rs.getObject(i);
                            row.add(val != null ? val.toString() : "NULL");
                        }
                        output.append(String.join("\t", row)).append("\n");
                        rowCount++;
                    }
                    output.append("\nRows: ").append(rowCount);
                }
            } else {
                int affected = stmt.executeUpdate(trimmedSql);
                output.append("Affected rows: ").append(affected);
            }

            return ExecutionResult.success(output.toString());

        } catch (SQLException e) {
            logger.error("SQL execution error for task {}: {}", taskNode.getId(), e.getMessage());
            return ExecutionResult.failure("SQL error: " + e.getMessage());
        }
    }

    private HikariDataSource getOrCreateDataSource(String jdbcUrl, String username, String password, String driver) {
        String cacheKey = jdbcUrl + "|" + username;
        return dataSourceCache.computeIfAbsent(cacheKey, key -> {
            HikariConfig config = new HikariConfig();
            config.setJdbcUrl(jdbcUrl);
            if (username != null) config.setUsername(username);
            if (password != null) config.setPassword(password);
            if (driver != null) config.setDriverClassName(driver);
            config.setMaximumPoolSize(5);
            config.setConnectionTimeout(10000);
            config.setIdleTimeout(30000);
            config.setMaxLifetime(600000);
            return new HikariDataSource(config);
        });
    }

    public void shutdown() {
        for (HikariDataSource ds : dataSourceCache.values()) {
            try {
                ds.close();
            } catch (Exception e) {
                logger.error("Error closing data source: {}", e.getMessage());
            }
        }
        dataSourceCache.clear();
    }
}
