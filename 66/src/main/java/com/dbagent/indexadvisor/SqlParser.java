package com.dbagent.indexadvisor;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class SqlParser {

    private static final Pattern TABLE_PATTERN = Pattern.compile(
            "(?i)\\bFROM\\s+([\\w.]+)|\\bJOIN\\s+([\\w.]+)|\\bINTO\\s+([\\w.]+)|\\bUPDATE\\s+([\\w.]+)",
            Pattern.CASE_INSENSITIVE);

    private static final Pattern WHERE_PATTERN = Pattern.compile(
            "(?is)\\bWHERE\\s+(.+?)(?:\\bGROUP\\s+BY\\b|\\bORDER\\s+BY\\b|\\bHAVING\\b|\\bLIMIT\\b|\\bOFFSET\\b|\\bUNION\\b|\\bFOR\\s+UPDATE\\b|\\bLOCK\\s+IN\\s+SHARE\\s+MODE\\b|$)",
            Pattern.CASE_INSENSITIVE);

    private static final Pattern JOIN_ON_PATTERN = Pattern.compile(
            "(?i)\\bON\\s+([\\w.]+)\\s*=\\s*([\\w.]+)",
            Pattern.CASE_INSENSITIVE);

    private static final Pattern COLUMN_PATTERN = Pattern.compile(
            "(?:[\\w]+\\.)?([\\w]+)\\s*(?:=|<=>|<>|!=|>=|<=|>|<|\\s+LIKE\\s+|\\s+IN\\s+|\\s+BETWEEN\\s+|\\s+IS\\s+)",
            Pattern.CASE_INSENSITIVE);

    private static final Pattern ORDER_BY_PATTERN = Pattern.compile(
            "(?i)\\bORDER\\s+BY\\s+(.+?)(?:\\bLIMIT\\b|\\bOFFSET\\b|$)",
            Pattern.CASE_INSENSITIVE);

    private static final Pattern GROUP_BY_PATTERN = Pattern.compile(
            "(?i)\\bGROUP\\s+BY\\s+(.+?)(?:\\bHAVING\\b|\\bORDER\\s+BY\\b|\\bLIMIT\\b|$)",
            Pattern.CASE_INSENSITIVE);

    private static final Set<String> SQL_KEYWORDS = new HashSet<>();

    static {
        Collections.addAll(SQL_KEYWORDS,
                "AND", "OR", "NOT", "NULL", "IS", "IN", "LIKE", "BETWEEN", "EXISTS",
                "TRUE", "FALSE", "NOW", "CURRENT_TIMESTAMP", "CURRENT_DATE", "CURRENT_TIME",
                "ASC", "DESC", "ON", "AS", "ALL", "ANY", "SOME", "DISTINCT",
                "SELECT", "FROM", "WHERE", "JOIN", "LEFT", "RIGHT", "INNER", "OUTER",
                "FULL", "CROSS", "GROUP", "BY", "ORDER", "HAVING", "LIMIT", "OFFSET",
                "UNION", "ALL", "EXCEPT", "INTERSECT", "FOR", "UPDATE", "LOCK",
                "SHARE", "MODE", "WITH", "RECURSIVE", "CASE", "WHEN", "THEN", "ELSE",
                "END", "COALESCE", "NULLIF", "GREATEST", "LEAST", "CAST", "CONVERT",
                "COUNT", "SUM", "AVG", "MIN", "MAX", "GROUP_CONCAT", "STRING_AGG",
                "ARRAY_AGG", "BOOL_AND", "BOOL_OR", "EVERY", "BIT_AND", "BIT_OR",
                "TRUE", "FALSE", "INTEGER", "VARCHAR", "TEXT", "TIMESTAMP", "DATE",
                "TIME", "BOOLEAN", "NUMERIC", "DECIMAL", "FLOAT", "DOUBLE", "BIGINT",
                "SMALLINT", "INT", "SERIAL", "BIGSERIAL", "UUID", "JSON", "JSONB",
                "XML", "BYTEA", "ARRAY", "HSTORE", "INET", "CIDR", "MACADDR",
                "INTERVAL", "POINT", "LINE", "LSEG", "BOX", "PATH", "POLYGON",
                "CIRCLE", "TSVECTOR", "TSQUERY", "RANGE", "MULTIRANGE"
        );
    }

    public static class ParsedSql {
        private List<String> tables = new ArrayList<>();
        private List<String> whereColumns = new ArrayList<>();
        private List<String> joinColumns = new ArrayList<>();
        private List<String> orderByColumns = new ArrayList<>();
        private List<String> groupByColumns = new ArrayList<>();
        private String operation = "UNKNOWN";

        public List<String> getTables() {
            return tables;
        }

        public List<String> getWhereColumns() {
            return whereColumns;
        }

        public List<String> getJoinColumns() {
            return joinColumns;
        }

        public List<String> getOrderByColumns() {
            return orderByColumns;
        }

        public List<String> getGroupByColumns() {
            return groupByColumns;
        }

        public String getOperation() {
            return operation;
        }

        public List<String> getAllIndexCandidateColumns() {
            Set<String> columns = new HashSet<>();
            columns.addAll(whereColumns);
            columns.addAll(joinColumns);
            return new ArrayList<>(columns);
        }

        public String getMainTable() {
            if (tables.isEmpty()) {
                return null;
            }
            return tables.get(0);
        }
    }

    public static ParsedSql parse(String sql) {
        ParsedSql parsed = new ParsedSql();

        if (sql == null || sql.trim().isEmpty()) {
            return parsed;
        }

        String normalized = normalizeSql(sql);

        parsed.operation = detectOperation(normalized);

        parsed.tables = extractTables(normalized);

        parsed.whereColumns = extractWhereColumns(normalized);

        parsed.joinColumns = extractJoinColumns(normalized);

        parsed.orderByColumns = extractOrderByColumns(normalized);

        parsed.groupByColumns = extractGroupByColumns(normalized);

        return parsed;
    }

    private static String normalizeSql(String sql) {
        String result = sql.replaceAll("(?s)/\\*.*?\\*/", " ");
        result = result.replaceAll("--[^\n]*", " ");
        result = result.replaceAll("\\s+", " ").trim();
        return result;
    }

    private static String detectOperation(String sql) {
        String upper = sql.trim().toUpperCase();
        if (upper.startsWith("SELECT")) {
            return "SELECT";
        } else if (upper.startsWith("INSERT")) {
            return "INSERT";
        } else if (upper.startsWith("UPDATE")) {
            return "UPDATE";
        } else if (upper.startsWith("DELETE")) {
            return "DELETE";
        } else if (upper.startsWith("REPLACE")) {
            return "REPLACE";
        } else if (upper.startsWith("WITH")) {
            return "WITH";
        }
        return "UNKNOWN";
    }

    private static List<String> extractTables(String sql) {
        List<String> tables = new ArrayList<>();
        Matcher matcher = TABLE_PATTERN.matcher(sql);
        while (matcher.find()) {
            for (int i = 1; i <= matcher.groupCount(); i++) {
                String table = matcher.group(i);
                if (table != null && !table.isEmpty()) {
                    String cleanTable = table.contains(".") ?
                            table.substring(table.lastIndexOf('.') + 1) : table;
                    if (!tables.contains(cleanTable) && !isKeyword(cleanTable)) {
                        tables.add(cleanTable);
                    }
                }
            }
        }
        return tables;
    }

    private static List<String> extractWhereColumns(String sql) {
        List<String> columns = new ArrayList<>();
        Matcher whereMatcher = WHERE_PATTERN.matcher(sql);
        if (whereMatcher.find()) {
            String whereClause = whereMatcher.group(1);
            extractColumnsFromClause(whereClause, columns);
        }
        return columns;
    }

    private static List<String> extractJoinColumns(String sql) {
        List<String> columns = new ArrayList<>();
        Matcher joinMatcher = JOIN_ON_PATTERN.matcher(sql);
        while (joinMatcher.find()) {
            for (int i = 1; i <= 2; i++) {
                String col = joinMatcher.group(i);
                if (col != null) {
                    String cleanCol = col.contains(".") ?
                            col.substring(col.lastIndexOf('.') + 1) : col;
                    if (!isKeyword(cleanCol) && !columns.contains(cleanCol)) {
                        columns.add(cleanCol);
                    }
                }
            }
        }
        return columns;
    }

    private static List<String> extractOrderByColumns(String sql) {
        List<String> columns = new ArrayList<>();
        Matcher matcher = ORDER_BY_PATTERN.matcher(sql);
        if (matcher.find()) {
            String orderBy = matcher.group(1);
            extractColumnsFromList(orderBy, columns);
        }
        return columns;
    }

    private static List<String> extractGroupByColumns(String sql) {
        List<String> columns = new ArrayList<>();
        Matcher matcher = GROUP_BY_PATTERN.matcher(sql);
        if (matcher.find()) {
            String groupBy = matcher.group(1);
            extractColumnsFromList(groupBy, columns);
        }
        return columns;
    }

    private static void extractColumnsFromClause(String clause, List<String> columns) {
        Matcher colMatcher = COLUMN_PATTERN.matcher(clause);
        while (colMatcher.find()) {
            String col = colMatcher.group(1);
            if (col != null && !isKeyword(col) && !columns.contains(col)) {
                columns.add(col);
            }
        }

        String[] parts = clause.split("(?i)\\s+(?:AND|OR)\\s+");
        for (String part : parts) {
            String trimmed = part.trim();
            int eqIdx = trimmed.indexOf('=');
            int cmpIdx = trimmed.indexOf("<=>");
            int likeIdx = trimmed.toUpperCase().indexOf(" LIKE ");
            int inIdx = trimmed.toUpperCase().indexOf(" IN ");
            int betweenIdx = trimmed.toUpperCase().indexOf(" BETWEEN ");
            int isIdx = trimmed.toUpperCase().indexOf(" IS ");

            int idx = -1;
            if (cmpIdx > 0) idx = cmpIdx;
            else if (eqIdx > 0) idx = eqIdx;
            else if (likeIdx > 0) idx = likeIdx;
            else if (inIdx > 0) idx = inIdx;
            else if (betweenIdx > 0) idx = betweenIdx;
            else if (isIdx > 0) idx = isIdx;

            if (idx > 0) {
                String leftPart = trimmed.substring(0, idx).trim();
                if (leftPart.contains(".")) {
                    leftPart = leftPart.substring(leftPart.lastIndexOf('.') + 1);
                }
                leftPart = leftPart.replaceAll("[()]", "").trim();
                if (!leftPart.isEmpty() && !isKeyword(leftPart) && !columns.contains(leftPart)) {
                    columns.add(leftPart);
                }
            }
        }
    }

    private static void extractColumnsFromList(String columnList, List<String> columns) {
        String[] parts = columnList.split(",");
        for (String part : parts) {
            String trimmed = part.trim();
            trimmed = trimmed.replaceAll("(?i)\\s+(ASC|DESC)$", "").trim();
            trimmed = trimmed.replaceAll("[()]", "").trim();
            if (trimmed.contains(".")) {
                trimmed = trimmed.substring(trimmed.lastIndexOf('.') + 1);
            }
            if (!trimmed.isEmpty() && !isKeyword(trimmed) && !columns.contains(trimmed)) {
                columns.add(trimmed);
            }
        }
    }

    private static boolean isKeyword(String word) {
        if (word == null || word.isEmpty()) {
            return true;
        }
        return SQL_KEYWORDS.contains(word.toUpperCase());
    }
}
