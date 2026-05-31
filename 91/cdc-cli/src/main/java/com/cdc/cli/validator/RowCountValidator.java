package com.cdc.cli.validator;

import com.cdc.cli.db.DatabaseConnectionManager;
import com.cdc.cli.model.ValidationConfig;
import com.cdc.cli.model.ValidationResult;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;

public class RowCountValidator {

    private static final Logger logger = LoggerFactory.getLogger(RowCountValidator.class);

    public ValidationResult validate(ValidationConfig config) {
        ValidationResult result = new ValidationResult();
        result.setTableName(config.getTableName());
        long startTime = System.currentTimeMillis();

        Connection sourceConn = null;
        Connection targetConn = null;

        try {
            sourceConn = DatabaseConnectionManager.getSourceConnection(config);
            targetConn = DatabaseConnectionManager.getTargetConnection(config);

            long sourceCount = getRowCount(sourceConn, config.getTableName(), config.getSourceType());
            long targetCount = getRowCount(targetConn, config.getTableName(), config.getTargetType());

            result.setSourceRowCount(sourceCount);
            result.setTargetRowCount(targetCount);
            result.setRowCountDiff(Math.abs(sourceCount - targetCount));
            result.setPassed(sourceCount == targetCount);

            logger.info("Row count validation for {}: source={}, target={}, diff={}, passed={}",
                    config.getTableName(), sourceCount, targetCount, result.getRowCountDiff(), result.isPassed());

        } catch (Exception e) {
            logger.error("Row count validation failed", e);
            result.setPassed(false);
            result.setErrorMessage(e.getMessage());
        } finally {
            DatabaseConnectionManager.closeQuietly(sourceConn);
            DatabaseConnectionManager.closeQuietly(targetConn);
            result.setDurationMs(System.currentTimeMillis() - startTime);
        }

        return result;
    }

    private long getRowCount(Connection conn, String tableName, String dbType) throws SQLException {
        String qualifiedTableName = qualifyTableName(tableName, dbType);
        String sql = "SELECT COUNT(*) FROM " + qualifiedTableName;

        logger.debug("Executing: {}", sql);

        try (PreparedStatement stmt = conn.prepareStatement(sql);
             ResultSet rs = stmt.executeQuery()) {
            if (rs.next()) {
                return rs.getLong(1);
            }
            return 0;
        }
    }

    private String qualifyTableName(String tableName, String dbType) {
        if (tableName.contains(".")) {
            String[] parts = tableName.split("\\.");
            if (parts.length == 2) {
                String schema = quoteIdentifier(parts[0], dbType);
                String table = quoteIdentifier(parts[1], dbType);
                return schema + "." + table;
            }
        }
        return quoteIdentifier(tableName, dbType);
    }

    private String quoteIdentifier(String identifier, String dbType) {
        if (identifier.startsWith("`") || identifier.startsWith("\"")) {
            return identifier;
        }
        switch (dbType.toLowerCase()) {
            case "mysql":
                return "`" + identifier + "`";
            case "postgresql":
            case "postgres":
                return "\"" + identifier + "\"";
            default:
                return identifier;
        }
    }
}
