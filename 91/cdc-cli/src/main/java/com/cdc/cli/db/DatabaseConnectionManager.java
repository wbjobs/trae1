package com.cdc.cli.db;

import com.cdc.cli.model.ValidationConfig;
import com.cdc.common.exception.CdcException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.util.Properties;

public class DatabaseConnectionManager {

    private static final Logger logger = LoggerFactory.getLogger(DatabaseConnectionManager.class);

    public static Connection getSourceConnection(ValidationConfig config) throws SQLException {
        return getConnection(
                config.getSourceType(),
                config.getSourceHost(),
                config.getSourcePort(),
                config.getSourceDatabase(),
                config.getSourceUsername(),
                config.getSourcePassword()
        );
    }

    public static Connection getTargetConnection(ValidationConfig config) throws SQLException {
        return getConnection(
                config.getTargetType(),
                config.getTargetHost(),
                config.getTargetPort(),
                config.getTargetDatabase(),
                config.getTargetUsername(),
                config.getTargetPassword()
        );
    }

    private static Connection getConnection(String type, String host, int port, String database,
                                            String username, String password) throws SQLException {
        String jdbcUrl = buildJdbcUrl(type, host, port, database);
        logger.info("Connecting to database: {}", jdbcUrl);

        Properties props = new Properties();
        props.setProperty("user", username);
        props.setProperty("password", password);
        props.setProperty("connectTimeout", "10000");
        props.setProperty("socketTimeout", "30000");

        loadDriver(type);

        return DriverManager.getConnection(jdbcUrl, props);
    }

    private static String buildJdbcUrl(String type, String host, int port, String database) {
        switch (type.toLowerCase()) {
            case "mysql":
                return String.format("jdbc:mysql://%s:%d/%s?useSSL=false&serverTimezone=UTC&useInformationSchema=true",
                        host, port, database);
            case "postgresql":
            case "postgres":
                return String.format("jdbc:postgresql://%s:%d/%s?sslmode=disable",
                        host, port, database);
            default:
                throw new CdcException("Unsupported database type: " + type);
        }
    }

    private static void loadDriver(String type) {
        try {
            switch (type.toLowerCase()) {
                case "mysql":
                    Class.forName("com.mysql.cj.jdbc.Driver");
                    break;
                case "postgresql":
                case "postgres":
                    Class.forName("org.postgresql.Driver");
                    break;
                default:
                    throw new CdcException("Unsupported database type: " + type);
            }
        } catch (ClassNotFoundException e) {
            throw new CdcException("JDBC driver not found for: " + type, e);
        }
    }

    public static void closeQuietly(Connection conn) {
        if (conn != null) {
            try {
                conn.close();
            } catch (SQLException e) {
                logger.warn("Failed to close connection", e);
            }
        }
    }
}
