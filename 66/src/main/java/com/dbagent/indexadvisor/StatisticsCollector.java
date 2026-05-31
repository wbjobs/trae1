package com.dbagent.indexadvisor;

import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

public class StatisticsCollector {

    private static volatile StatisticsCollector instance;
    private static final Object LOCK = new Object();

    private final Map<String, TableStatistics> statisticsCache = new ConcurrentHashMap<>();
    private final Map<String, Long> lastUpdateTime = new ConcurrentHashMap<>();
    private final AtomicLong totalTablesAnalyzed = new AtomicLong(0);

    private ScheduledExecutorService refreshExecutor;
    private volatile boolean initialized = false;
    private long cacheTtlMs = 300000;

    private StatisticsCollector() {
    }

    public static StatisticsCollector getInstance() {
        if (instance == null) {
            synchronized (LOCK) {
                if (instance == null) {
                    instance = new StatisticsCollector();
                }
            }
        }
        return instance;
    }

    public void initialize(long cacheTtlMs, long refreshIntervalMs) {
        if (initialized) {
            return;
        }
        this.cacheTtlMs = cacheTtlMs > 0 ? cacheTtlMs : 300000;

        if (refreshIntervalMs > 0) {
            refreshExecutor = Executors.newSingleThreadScheduledExecutor(r -> {
                Thread t = new Thread(r, "stats-collector-refresh");
                t.setDaemon(true);
                return t;
            });
            refreshExecutor.scheduleAtFixedRate(this::refreshAll,
                    refreshIntervalMs, refreshIntervalMs, TimeUnit.MILLISECONDS);
        }

        initialized = true;
        System.out.println("[DB-Tracing-Agent] StatisticsCollector initialized, cache TTL: "
                + this.cacheTtlMs + "ms");
    }

    public TableStatistics collect(Connection connection, String tableName) {
        if (connection == null || tableName == null) {
            return TableStatistics.EMPTY;
        }

        String cacheKey = getCacheKey(connection, tableName);
        long now = System.currentTimeMillis();
        Long lastUpdate = lastUpdateTime.get(cacheKey);

        if (lastUpdate != null && (now - lastUpdate) < cacheTtlMs) {
            TableStatistics cached = statisticsCache.get(cacheKey);
            if (cached != null) {
                return cached;
            }
        }

        TableStatistics stats = collectFromDatabase(connection, tableName);
        if (stats != null) {
            statisticsCache.put(cacheKey, stats);
            lastUpdateTime.put(cacheKey, now);
            totalTablesAnalyzed.incrementAndGet();
        }
        return stats != null ? stats : TableStatistics.EMPTY;
    }

    private TableStatistics collectFromDatabase(Connection connection, String tableName) {
        try {
            TableStatistics stats = new TableStatistics();
            stats.setTableName(tableName);

            DatabaseMetaData metaData = connection.getMetaData();
            String databaseType = metaData.getDatabaseProductName();
            stats.setDatabaseType(databaseType);

            collectColumnStatistics(connection, tableName, stats);
            collectIndexStatistics(connection, metaData, tableName, stats);
            collectTableRowCount(connection, metaData, tableName, stats);
            collectPgStatStatements(connection, tableName, stats);
            collectPerformanceSchema(connection, tableName, stats);

            return stats;
        } catch (Exception e) {
            System.err.println("[DB-Tracing-Agent] Error collecting statistics for "
                    + tableName + ": " + e.getMessage());
            return TableStatistics.EMPTY;
        }
    }

    private void collectColumnStatistics(Connection connection, String tableName,
                                         TableStatistics stats) {
        try {
            DatabaseMetaData metaData = connection.getMetaData();
            String catalog = connection.getCatalog();
            String schema = null;

            try {
                if (connection.getSchema() != null) {
                    schema = connection.getSchema();
                }
            } catch (Exception e) {
                // ignore
            }

            ResultSet columns = metaData.getColumns(catalog, schema, tableName, null);
            while (columns.next()) {
                String columnName = columns.getString("COLUMN_NAME");
                String typeName = columns.getString("TYPE_NAME");
                int columnSize = columns.getInt("COLUMN_SIZE");
                String nullable = columns.getString("IS_NULLABLE");

                ColumnStatistics colStats = new ColumnStatistics();
                colStats.setColumnName(columnName);
                colStats.setDataType(typeName);
                colStats.setColumnSize(columnSize);
                colStats.setNullable("YES".equalsIgnoreCase(nullable));
                stats.addColumn(colStats);
            }
            columns.close();
        } catch (Exception e) {
            // ignore individual column errors
        }
    }

    private void collectIndexStatistics(Connection connection, DatabaseMetaData metaData,
                                        String tableName, TableStatistics stats) {
        try {
            String catalog = connection.getCatalog();
            String schema = null;
            try {
                if (connection.getSchema() != null) {
                    schema = connection.getSchema();
                }
            } catch (Exception e) {
                // ignore
            }

            ResultSet indexes = metaData.getIndexInfo(catalog, schema, tableName, false, false);
            while (indexes.next()) {
                String indexName = indexes.getString("INDEX_NAME");
                String columnName = indexes.getString("COLUMN_NAME");
                boolean nonUnique = indexes.getBoolean("NON_UNIQUE");
                short type = indexes.getShort("TYPE");

                if (indexName != null && columnName != null) {
                    stats.addExistingIndexColumn(columnName);
                    if (!nonUnique && type == java.sql.DatabaseMetaData.tableIndexStatistic) {
                        stats.addUniqueColumn(columnName);
                    }
                }
            }
            indexes.close();
        } catch (Exception e) {
            // ignore
        }
    }

    private void collectTableRowCount(Connection connection, DatabaseMetaData metaData,
                                      String tableName, TableStatistics stats) {
        try {
            String databaseType = metaData.getDatabaseProductName();
            String countSql;

            if ("PostgreSQL".equalsIgnoreCase(databaseType)) {
                countSql = "SELECT reltuples::bigint FROM pg_class WHERE relname = ?";
            } else if ("MySQL".equalsIgnoreCase(databaseType)) {
                countSql = "SELECT table_rows FROM information_schema.tables WHERE table_name = ? AND table_schema = DATABASE()";
            } else {
                countSql = "SELECT COUNT(*) FROM " + tableName;
            }

            PreparedStatement ps = connection.prepareStatement(countSql);
            ps.setString(1, tableName);
            ResultSet rs = ps.executeQuery();
            if (rs.next()) {
                stats.setRowCount(rs.getLong(1));
            }
            rs.close();
            ps.close();
        } catch (Exception e) {
            // ignore
        }
    }

    private void collectPgStatStatements(Connection connection, String tableName,
                                         TableStatistics stats) {
        try {
            String sql = "SELECT calls, total_exec_time, mean_exec_time, rows " +
                    "FROM pg_stat_statements WHERE query LIKE ? LIMIT 1";
            PreparedStatement ps = connection.prepareStatement(sql);
            ps.setString(1, "%" + tableName + "%");
            ResultSet rs = ps.executeQuery();
            if (rs.next()) {
                stats.setPgStatCalls(rs.getLong("calls"));
                stats.setPgStatTotalExecTime(rs.getDouble("total_exec_time"));
                stats.setPgStatMeanExecTime(rs.getDouble("mean_exec_time"));
                stats.setPgStatRows(rs.getLong("rows"));
            }
            rs.close();
            ps.close();
        } catch (Exception e) {
            // pg_stat_statements might not be available
        }
    }

    private void collectPerformanceSchema(Connection connection, String tableName,
                                          TableStatistics stats) {
        try {
            String sql = "SELECT COUNT_STAR, SUM_TIMER_WAIT, SUM_ROWS_EXAMINED " +
                    "FROM performance_schema.table_io_waits_summary_by_table " +
                    "WHERE OBJECT_NAME = ? LIMIT 1";
            PreparedStatement ps = connection.prepareStatement(sql);
            ps.setString(1, tableName);
            ResultSet rs = ps.executeQuery();
            if (rs.next()) {
                stats.setPerfSchemaCountStar(rs.getLong("COUNT_STAR"));
                stats.setPerfSchemaSumTimerWait(rs.getLong("SUM_TIMER_WAIT"));
                stats.setPerfSchemaSumRowsExamined(rs.getLong("SUM_ROWS_EXAMINED"));
            }
            rs.close();
            ps.close();
        } catch (Exception e) {
            // performance_schema might not be available
        }
    }

    private String getCacheKey(Connection connection, String tableName) {
        try {
            String dbName = connection.getCatalog();
            String schema = connection.getSchema();
            return (dbName != null ? dbName : "") + ":" +
                    (schema != null ? schema : "") + ":" + tableName;
        } catch (Exception e) {
            return tableName;
        }
    }

    private void refreshAll() {
        long now = System.currentTimeMillis();
        for (Map.Entry<String, Long> entry : lastUpdateTime.entrySet()) {
            if (now - entry.getValue() > cacheTtlMs) {
                statisticsCache.remove(entry.getKey());
                lastUpdateTime.remove(entry.getKey());
            }
        }
    }

    public long getTotalTablesAnalyzed() {
        return totalTablesAnalyzed.get();
    }

    public void shutdown() {
        if (refreshExecutor != null) {
            refreshExecutor.shutdown();
            refreshExecutor = null;
        }
        statisticsCache.clear();
        lastUpdateTime.clear();
        initialized = false;
    }

    public static class TableStatistics {
        public static final TableStatistics EMPTY = new TableStatistics();

        private String tableName;
        private String databaseType;
        private long rowCount;
        private List<ColumnStatistics> columns = new ArrayList<>();
        private Set<String> existingIndexColumns = new HashSet<>();
        private Set<String> uniqueColumns = new HashSet<>();
        private long pgStatCalls;
        private double pgStatTotalExecTime;
        private double pgStatMeanExecTime;
        private long pgStatRows;
        private long perfSchemaCountStar;
        private long perfSchemaSumTimerWait;
        private long perfSchemaSumRowsExamined;

        public String getTableName() {
            return tableName;
        }

        public void setTableName(String tableName) {
            this.tableName = tableName;
        }

        public String getDatabaseType() {
            return databaseType;
        }

        public void setDatabaseType(String databaseType) {
            this.databaseType = databaseType;
        }

        public long getRowCount() {
            return rowCount;
        }

        public void setRowCount(long rowCount) {
            this.rowCount = rowCount;
        }

        public List<ColumnStatistics> getColumns() {
            return columns;
        }

        public void setColumns(List<ColumnStatistics> columns) {
            this.columns = columns;
        }

        public void addColumn(ColumnStatistics column) {
            this.columns.add(column);
        }

        public Set<String> getExistingIndexColumns() {
            return existingIndexColumns;
        }

        public void addExistingIndexColumn(String column) {
            this.existingIndexColumns.add(column);
        }

        public Set<String> getUniqueColumns() {
            return uniqueColumns;
        }

        public void addUniqueColumn(String column) {
            this.uniqueColumns.add(column);
        }

        public boolean hasIndexOn(String column) {
            return existingIndexColumns.contains(column);
        }

        public long getPgStatCalls() {
            return pgStatCalls;
        }

        public void setPgStatCalls(long pgStatCalls) {
            this.pgStatCalls = pgStatCalls;
        }

        public double getPgStatMeanExecTime() {
            return pgStatMeanExecTime;
        }

        public void setPgStatMeanExecTime(double pgStatMeanExecTime) {
            this.pgStatMeanExecTime = pgStatMeanExecTime;
        }

        public long getPerfSchemaCountStar() {
            return perfSchemaCountStar;
        }

        public void setPerfSchemaCountStar(long perfSchemaCountStar) {
            this.perfSchemaCountStar = perfSchemaCountStar;
        }

        public long getPerfSchemaSumRowsExamined() {
            return perfSchemaSumRowsExamined;
        }

        public void setPerfSchemaSumRowsExamined(long perfSchemaSumRowsExamined) {
            this.perfSchemaSumRowsExamined = perfSchemaSumRowsExamined;
        }
    }

    public static class ColumnStatistics {
        private String columnName;
        private String dataType;
        private int columnSize;
        private boolean nullable;
        private long distinctValues;
        private long nullCount;

        public String getColumnName() {
            return columnName;
        }

        public void setColumnName(String columnName) {
            this.columnName = columnName;
        }

        public String getDataType() {
            return dataType;
        }

        public void setDataType(String dataType) {
            this.dataType = dataType;
        }

        public int getColumnSize() {
            return columnSize;
        }

        public void setColumnSize(int columnSize) {
            this.columnSize = columnSize;
        }

        public boolean isNullable() {
            return nullable;
        }

        public void setNullable(boolean nullable) {
            this.nullable = nullable;
        }

        public long getDistinctValues() {
            return distinctValues;
        }

        public void setDistinctValues(long distinctValues) {
            this.distinctValues = distinctValues;
        }

        public long getNullCount() {
            return nullCount;
        }

        public void setNullCount(long nullCount) {
            this.nullCount = nullCount;
        }
    }
}
