package com.cdc.cli.validator;

import com.cdc.cli.db.DatabaseConnectionManager;
import com.cdc.cli.model.ValidationConfig;
import com.cdc.cli.model.ValidationResult;
import com.google.common.hash.HashFunction;
import com.google.common.hash.Hashing;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.nio.charset.StandardCharsets;
import java.sql.*;
import java.util.*;

public class SampleDataValidator {

    private static final Logger logger = LoggerFactory.getLogger(SampleDataValidator.class);
    private static final HashFunction HASH_FUNCTION = Hashing.murmur3_128();

    public ValidationResult validate(ValidationConfig config) {
        ValidationResult result = new ValidationResult();
        result.setTableName(config.getTableName());
        long startTime = System.currentTimeMillis();

        Connection sourceConn = null;
        Connection targetConn = null;

        try {
            sourceConn = DatabaseConnectionManager.getSourceConnection(config);
            targetConn = DatabaseConnectionManager.getTargetConnection(config);

            String primaryKey = findPrimaryKey(sourceConn, config.getTableName(), config.getSourceType());
            if (primaryKey == null) {
                result.setPassed(false);
                result.setErrorMessage("No primary key found for table: " + config.getTableName());
                return result;
            }

            List<String> columns = getColumns(sourceConn, config.getTableName(), config.getSourceType(),
                    config.getIncludedColumns());
            if (columns.isEmpty()) {
                result.setPassed(false);
                result.setErrorMessage("No columns found for table: " + config.getTableName());
                return result;
            }

            List<Object> sampleIds = getSampleIds(sourceConn, config, primaryKey);
            result.setSampleSize(sampleIds.size());

            if (sampleIds.isEmpty()) {
                result.setPassed(true);
                logger.info("No data to sample for table: {}", config.getTableName());
                return result;
            }

            int mismatches = 0;
            Map<Object, Map<String, Object>> sourceRows = fetchRows(sourceConn, config, primaryKey, columns, sampleIds);
            Map<Object, Map<String, Object>> targetRows = fetchRows(targetConn, config, primaryKey, columns, sampleIds);

            for (Object id : sampleIds) {
                Map<String, Object> sourceRow = sourceRows.get(id);
                Map<String, Object> targetRow = targetRows.get(id);

                if (sourceRow == null || targetRow == null) {
                    mismatches++;
                    result.addMismatch(new ValidationResult.RowMismatch(
                            String.valueOf(id),
                            "ROW_EXISTENCE",
                            sourceRow != null ? "EXISTS" : "MISSING",
                            targetRow != null ? "EXISTS" : "MISSING"
                    ));
                    continue;
                }

                String sourceHash = hashRow(sourceRow, columns);
                String targetHash = hashRow(targetRow, columns);

                if (!sourceHash.equals(targetHash)) {
                    mismatches++;
                    for (String col : columns) {
                        Object sourceVal = sourceRow.get(col);
                        Object targetVal = targetRow.get(col);
                        if (!Objects.equals(sourceVal, targetVal)) {
                            result.addMismatch(new ValidationResult.RowMismatch(
                                    String.valueOf(id), col, sourceVal, targetVal
                            ));
                        }
                    }
                }
            }

            result.setMismatchedSamples(mismatches);
            result.setPassed(mismatches == 0);

            logger.info("Sample validation for {}: samples={}, mismatches={}, passed={}",
                    config.getTableName(), sampleIds.size(), mismatches, result.isPassed());

        } catch (Exception e) {
            logger.error("Sample data validation failed", e);
            result.setPassed(false);
            result.setErrorMessage(e.getMessage());
        } finally {
            DatabaseConnectionManager.closeQuietly(sourceConn);
            DatabaseConnectionManager.closeQuietly(targetConn);
            result.setDurationMs(System.currentTimeMillis() - startTime);
        }

        return result;
    }

    private String findPrimaryKey(Connection conn, String tableName, String dbType) throws SQLException {
        String[] parts = tableName.split("\\.");
        String schema = parts.length > 1 ? parts[0] : null;
        String table = parts.length > 1 ? parts[1] : tableName;

        try (ResultSet rs = conn.getMetaData().getPrimaryKeys(null, schema, table)) {
            if (rs.next()) {
                return rs.getString("COLUMN_NAME");
            }
        }

        try (ResultSet rs = conn.getMetaData().getPrimaryKeys(null, schema != null ? schema.toUpperCase() : null,
                table.toUpperCase())) {
            if (rs.next()) {
                return rs.getString("COLUMN_NAME");
            }
        }

        logger.warn("No primary key found, falling back to 'id' column");
        return "id";
    }

    private List<String> getColumns(Connection conn, String tableName, String dbType, String[] includedColumns) throws SQLException {
        List<String> columns = new ArrayList<>();
        String[] parts = tableName.split("\\.");
        String schema = parts.length > 1 ? parts[0] : null;
        String table = parts.length > 1 ? parts[1] : tableName;

        Set<String> includedSet = includedColumns != null && includedColumns.length > 0
                ? new HashSet<>(Arrays.asList(includedColumns))
                : null;

        try (ResultSet rs = conn.getMetaData().getColumns(null, schema, table, null)) {
            while (rs.next()) {
                String colName = rs.getString("COLUMN_NAME");
                if (includedSet == null || includedSet.contains(colName)) {
                    columns.add(colName);
                }
            }
        }

        if (columns.isEmpty()) {
            try (ResultSet rs = conn.getMetaData().getColumns(null,
                    schema != null ? schema.toUpperCase() : null,
                    table.toUpperCase(), null)) {
                while (rs.next()) {
                    String colName = rs.getString("COLUMN_NAME");
                    if (includedSet == null || includedSet.contains(colName)) {
                        columns.add(colName);
                    }
                }
            }
        }

        return columns;
    }

    private List<Object> getSampleIds(Connection conn, ValidationConfig config, String primaryKey) throws SQLException {
        String qualifiedTableName = qualifyTableName(config.getTableName(), config.getSourceType());
        String sql;

        if (config.getSourceType().equalsIgnoreCase("mysql")) {
            sql = String.format("SELECT %s FROM %s ORDER BY RAND() LIMIT ?", primaryKey, qualifiedTableName);
        } else if (config.getSourceType().equalsIgnoreCase("postgresql") ||
                config.getSourceType().equalsIgnoreCase("postgres")) {
            sql = String.format("SELECT %s FROM %s ORDER BY RANDOM() LIMIT ?", primaryKey, qualifiedTableName);
        } else {
            sql = String.format("SELECT %s FROM %s LIMIT ?", primaryKey, qualifiedTableName);
        }

        List<Object> ids = new ArrayList<>();
        try (PreparedStatement stmt = conn.prepareStatement(sql)) {
            stmt.setInt(1, config.getSampleSize());
            try (ResultSet rs = stmt.executeQuery()) {
                while (rs.next()) {
                    ids.add(rs.getObject(1));
                }
            }
        }

        logger.debug("Sampled {} IDs from {}", ids.size(), config.getTableName());
        return ids;
    }

    private Map<Object, Map<String, Object>> fetchRows(Connection conn, ValidationConfig config,
                                                        String primaryKey, List<String> columns,
                                                        List<Object> ids) throws SQLException {
        String qualifiedTableName = qualifyTableName(config.getTableName(), config.getSourceType());
        String placeholders = String.join(",", Collections.nCopies(ids.size(), "?"));
        String columnList = String.join(",", columns);

        String sql = String.format("SELECT %s FROM %s WHERE %s IN (%s)",
                columnList, qualifiedTableName, primaryKey, placeholders);

        Map<Object, Map<String, Object>> rows = new HashMap<>();
        try (PreparedStatement stmt = conn.prepareStatement(sql)) {
            for (int i = 0; i < ids.size(); i++) {
                stmt.setObject(i + 1, ids.get(i));
            }
            try (ResultSet rs = stmt.executeQuery()) {
                ResultSetMetaData md = rs.getMetaData();
                while (rs.next()) {
                    Map<String, Object> row = new HashMap<>();
                    Object id = null;
                    for (int i = 1; i <= md.getColumnCount(); i++) {
                        String colName = md.getColumnName(i);
                        Object value = rs.getObject(i);
                        row.put(colName, value);
                        if (colName.equalsIgnoreCase(primaryKey)) {
                            id = value;
                        }
                    }
                    if (id != null) {
                        rows.put(id, row);
                    }
                }
            }
        }

        return rows;
    }

    private String hashRow(Map<String, Object> row, List<String> columns) {
        StringBuilder sb = new StringBuilder();
        for (String col : columns) {
            Object val = row.get(col);
            sb.append(col).append("=").append(val != null ? val.toString() : "NULL").append("|");
        }
        return HASH_FUNCTION.hashString(sb.toString(), StandardCharsets.UTF_8).toString();
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
