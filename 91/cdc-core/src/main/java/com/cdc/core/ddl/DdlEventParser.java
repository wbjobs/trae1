package com.cdc.core.ddl;

import com.cdc.common.event.DdlEvent;
import com.cdc.common.event.DdlEvent.ColumnChange;
import com.cdc.common.event.DdlEvent.ColumnChangeType;
import com.cdc.common.event.DdlEvent.DdlType;
import io.debezium.antlr.AntlrDdlParserListener;
import io.debezium.ddl.parser.mysql.MySqlDdlParser;
import io.debezium.ddl.parser.postgresql.PostgresDdlParser;
import io.debezium.relational.Column;
import io.debezium.relational.Table;
import io.debezium.relational.TableId;
import io.debezium.relational.Tables;
import io.debezium.text.ParsingException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class DdlEventParser {

    private static final Logger logger = LoggerFactory.getLogger(DdlEventParser.class);

    private static final Pattern ADD_COLUMN_PATTERN = Pattern.compile(
            "(?i)ADD\\s+(?:COLUMN\\s+)?IF\\s+NOT\\s+EXISTS\\s+(\\w+)\\s+(\\w+(?:\\s*\\([^)]*\\))?)",
            Pattern.CASE_INSENSITIVE);

    private static final Pattern DROP_COLUMN_PATTERN = Pattern.compile(
            "(?i)DROP\\s+(?:COLUMN\\s+)?IF\\s+EXISTS\\s+(\\w+)",
            Pattern.CASE_INSENSITIVE);

    private static final Pattern ALTER_COLUMN_TYPE_PATTERN = Pattern.compile(
            "(?i)(?:ALTER|MODIFY)\\s+(?:COLUMN\\s+)?(\\w+)\\s+(?:SET\\s+DATA\\s+TYPE\\s+)?(\\w+(?:\\s*\\([^)]*\\))?)",
            Pattern.CASE_INSENSITIVE);

    private static final Pattern RENAME_COLUMN_PATTERN = Pattern.compile(
            "(?i)(?:RENAME\\s+COLUMN\\s+)?(\\w+)\\s+TO\\s+(\\w+)",
            Pattern.CASE_INSENSITIVE);

    public static DdlEvent parse(String database, String schema, String tableName, String ddlSql, String dbType) {
        DdlEvent event = new DdlEvent();
        event.setSourceDatabase(database);
        event.setSchemaName(schema);
        event.setTableName(tableName);
        event.setRawSql(ddlSql);
        event.setTimestamp(java.time.Instant.now());

        String normalizedSql = normalizeSql(ddlSql);
        event.setDdlType(determineDdlType(normalizedSql));
        event.setColumnChanges(parseColumnChanges(normalizedSql, event.getDdlType()));

        logger.info("Parsed DDL event: type={}, table={}, columns={}",
                event.getDdlType(), event.getFullTableName(), event.getColumnChanges().size());

        return event;
    }

    private static String normalizeSql(String sql) {
        if (sql == null) {
            return "";
        }
        return sql.replaceAll("\\s+", " ").trim();
    }

    private static DdlType determineDdlType(String sql) {
        String upperSql = sql.toUpperCase();

        if (upperSql.contains("CREATE TABLE")) {
            return DdlType.CREATE_TABLE;
        }
        if (upperSql.contains("DROP TABLE")) {
            return DdlType.DROP_TABLE;
        }
        if (upperSql.contains("ADD COLUMN") || upperSql.matches("(?i).*ALTER\\s+TABLE\\s+\\w+\\s+ADD\\s+(?!\\s*INDEX|KEY|CONSTRAINT).*")) {
            return DdlType.ADD_COLUMN;
        }
        if (upperSql.contains("DROP COLUMN") || upperSql.matches("(?i).*ALTER\\s+TABLE\\s+\\w+\\s+DROP\\s+(?!\\s*INDEX|KEY|CONSTRAINT).*")) {
            return DdlType.DROP_COLUMN;
        }
        if (upperSql.contains("ALTER COLUMN") || upperSql.contains("MODIFY COLUMN") ||
                upperSql.matches("(?i).*ALTER\\s+TABLE\\s+\\w+\\s+(?:ALTER|MODIFY)\\s+.*")) {
            if (upperSql.contains("TYPE") || upperSql.contains("DATA TYPE")) {
                return DdlType.ALTER_COLUMN_TYPE;
            }
            return DdlType.ALTER_TABLE;
        }
        if (upperSql.contains("RENAME COLUMN") || upperSql.matches("(?i).*ALTER\\s+TABLE\\s+\\w+\\s+RENAME\\s+.*")) {
            return DdlType.RENAME_COLUMN;
        }
        if (upperSql.contains("ALTER TABLE")) {
            return DdlType.ALTER_TABLE;
        }

        return DdlType.OTHER;
    }

    private static List<ColumnChange> parseColumnChanges(String sql, DdlType ddlType) {
        List<ColumnChange> changes = new ArrayList<>();

        switch (ddlType) {
            case ADD_COLUMN:
                parseAddColumn(sql, changes);
                break;
            case DROP_COLUMN:
                parseDropColumn(sql, changes);
                break;
            case ALTER_COLUMN_TYPE:
                parseAlterColumnType(sql, changes);
                break;
            case RENAME_COLUMN:
                parseRenameColumn(sql, changes);
                break;
            case ALTER_TABLE:
                parseAlterTable(sql, changes);
                break;
            default:
                break;
        }

        return changes;
    }

    private static void parseAddColumn(String sql, List<ColumnChange> changes) {
        Matcher matcher = ADD_COLUMN_PATTERN.matcher(sql);
        while (matcher.find()) {
            ColumnChange change = new ColumnChange();
            change.setChangeType(ColumnChangeType.ADD);
            change.setColumnName(matcher.group(1));
            change.setNewType(normalizeType(matcher.group(2)));
            change.setNewNullable(true);
            changes.add(change);
            logger.debug("Parsed ADD COLUMN: name={}, type={}", change.getColumnName(), change.getNewType());
        }
    }

    private static void parseDropColumn(String sql, List<ColumnChange> changes) {
        Matcher matcher = DROP_COLUMN_PATTERN.matcher(sql);
        while (matcher.find()) {
            ColumnChange change = new ColumnChange();
            change.setChangeType(ColumnChangeType.DROP);
            change.setColumnName(matcher.group(1));
            changes.add(change);
            logger.debug("Parsed DROP COLUMN: name={}", change.getColumnName());
        }
    }

    private static void parseAlterColumnType(String sql, List<ColumnChange> changes) {
        Matcher matcher = ALTER_COLUMN_TYPE_PATTERN.matcher(sql);
        if (matcher.find()) {
            ColumnChange change = new ColumnChange();
            change.setChangeType(ColumnChangeType.MODIFY_TYPE);
            change.setColumnName(matcher.group(1));
            change.setNewType(normalizeType(matcher.group(2)));
            changes.add(change);
            logger.debug("Parsed ALTER COLUMN TYPE: name={}, newType={}", change.getColumnName(), change.getNewType());
        }
    }

    private static void parseRenameColumn(String sql, List<ColumnChange> changes) {
        Matcher matcher = RENAME_COLUMN_PATTERN.matcher(sql);
        if (matcher.find()) {
            ColumnChange change = new ColumnChange();
            change.setChangeType(ColumnChangeType.RENAME);
            change.setOldColumnName(matcher.group(1));
            change.setColumnName(matcher.group(2));
            changes.add(change);
            logger.debug("Parsed RENAME COLUMN: oldName={}, newName={}",
                    change.getOldColumnName(), change.getColumnName());
        }
    }

    private static void parseAlterTable(String sql, List<ColumnChange> changes) {
        parseAddColumn(sql, changes);
        parseDropColumn(sql, changes);
        parseAlterColumnType(sql, changes);
        parseRenameColumn(sql, changes);

        if (changes.isEmpty()) {
            logger.debug("No specific column changes parsed from ALTER TABLE: {}", sql);
        }
    }

    private static String normalizeType(String type) {
        if (type == null) {
            return null;
        }
        String normalized = type.trim().toUpperCase();
        if (normalized.contains("INT") && !normalized.contains("BIGINT") && !normalized.contains("TINYINT") && !normalized.contains("SMALLINT")) {
            return "INT";
        }
        if (normalized.contains("BIGINT")) {
            return "BIGINT";
        }
        if (normalized.contains("TINYINT")) {
            return "TINYINT";
        }
        if (normalized.contains("SMALLINT")) {
            return "SMALLINT";
        }
        if (normalized.contains("VARCHAR") || normalized.contains("CHAR")) {
            return "VARCHAR";
        }
        if (normalized.contains("TEXT")) {
            return "TEXT";
        }
        if (normalized.contains("DECIMAL") || normalized.contains("NUMERIC")) {
            return "DECIMAL";
        }
        if (normalized.contains("TIMESTAMP") || normalized.contains("DATETIME")) {
            return "TIMESTAMP";
        }
        if (normalized.contains("DATE")) {
            return "DATE";
        }
        if (normalized.contains("TIME")) {
            return "TIME";
        }
        if (normalized.contains("BOOLEAN") || normalized.contains("BOOL")) {
            return "BOOLEAN";
        }
        if (normalized.contains("JSON")) {
            return "JSON";
        }
        if (normalized.contains("BLOB") || normalized.contains("BINARY") || normalized.contains("BYTEA")) {
            return "BLOB";
        }
        if (normalized.contains("FLOAT") || normalized.contains("REAL")) {
            return "FLOAT";
        }
        if (normalized.contains("DOUBLE")) {
            return "DOUBLE";
        }
        return normalized;
    }

    public static boolean isSafeTypeConversion(String oldType, String newType) {
        if (oldType == null || newType == null) {
            return false;
        }

        String oldNorm = normalizeType(oldType);
        String newNorm = normalizeType(newType);

        if (oldNorm.equals(newNorm)) {
            return true;
        }

        if (oldNorm.equals("TINYINT") && (newNorm.equals("SMALLINT") || newNorm.equals("INT") || newNorm.equals("BIGINT"))) {
            return true;
        }
        if (oldNorm.equals("SMALLINT") && (newNorm.equals("INT") || newNorm.equals("BIGINT"))) {
            return true;
        }
        if (oldNorm.equals("INT") && newNorm.equals("BIGINT")) {
            return true;
        }
        if (oldNorm.equals("FLOAT") && newNorm.equals("DOUBLE")) {
            return true;
        }
        if (oldNorm.equals("CHAR") && newNorm.equals("VARCHAR")) {
            return true;
        }
        if (oldNorm.equals("VARCHAR") && newNorm.equals("TEXT")) {
            return true;
        }
        if (oldNorm.equals("DATE") && (newNorm.equals("TIMESTAMP") || newNorm.equals("DATETIME"))) {
            return true;
        }
        if (oldNorm.equals("TIME") && newNorm.equals("TIMESTAMP")) {
            return true;
        }

        logger.warn("Unsafe type conversion: {} -> {}", oldNorm, newNorm);
        return false;
    }
}
