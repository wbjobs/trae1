package com.dbagent.interceptor;

import com.dbagent.config.AgentConfig;
import com.dbagent.indexadvisor.SlowSqlDetector;
import com.dbagent.masking.MaskingConfig;
import com.dbagent.masking.SensitiveDataMasker;
import com.dbagent.tracing.TraceContext;
import com.dbagent.tracing.TracingInitializer;
import io.opentelemetry.api.common.AttributeKey;
import io.opentelemetry.api.trace.Span;
import io.opentelemetry.api.trace.StatusCode;
import io.opentelemetry.api.trace.Tracer;
import io.opentelemetry.context.Context;
import io.opentelemetry.context.Scope;
import net.bytebuddy.implementation.bind.annotation.Argument;
import net.bytebuddy.implementation.bind.annotation.Origin;
import net.bytebuddy.implementation.bind.annotation.RuntimeType;
import net.bytebuddy.implementation.bind.annotation.SuperCall;
import net.bytebuddy.implementation.bind.annotation.This;

import java.lang.reflect.Method;
import java.sql.Connection;
import java.sql.Statement;
import java.util.concurrent.Callable;

public class StatementInterceptor {

    private static final AttributeKey<String> DB_SYSTEM = AttributeKey.stringKey("db.system");
    private static final AttributeKey<String> DB_NAME = AttributeKey.stringKey("db.name");
    private static final AttributeKey<String> DB_USER = AttributeKey.stringKey("db.user");
    private static final AttributeKey<String> DB_STATEMENT = AttributeKey.stringKey("db.statement");
    private static final AttributeKey<String> DB_CONNECTION_STRING = AttributeKey.stringKey("db.connection_string");
    private static final AttributeKey<Long> DB_ROWS_RETURNED = AttributeKey.longKey("db.rows_returned");
    private static final AttributeKey<String> CALL_STACK = AttributeKey.stringKey("db.call_stack");

    private static volatile AgentConfig config;

    public static void setConfig(AgentConfig agentConfig) {
        config = agentConfig;
    }

    @RuntimeType
    public static Object intercept(
            @SuperCall Callable<?> zuper,
            @Origin Method method,
            @This(optional = true) Object self,
            @Argument(value = 0, optional = true) String sql
    ) throws Exception {

        String methodName = method.getName();
        boolean isExecuteMethod = isExecuteMethod(methodName);

        if (!isExecuteMethod) {
            return zuper.call();
        }

        if (!TracingInitializer.isInitialized()) {
            return zuper.call();
        }

        Tracer tracer = TracingInitializer.getTracer();
        String operationName = extractOperationName(sql, methodName);
        String dbSystem = detectDbSystem(self);

        Connection connection = null;
        String dbUrl = null;
        String dbName = null;
        if (self instanceof Statement) {
            try {
                connection = ((Statement) self).getConnection();
                if (connection != null) {
                    dbUrl = connection.getMetaData().getURL();
                    dbName = extractDbName(dbUrl);
                }
            } catch (Exception e) {
                // ignore
            }
        }

        Context parentContext = TraceContext.getParentContext();
        Span span = tracer.spanBuilder(operationName)
                .setParent(parentContext)
                .setAttribute(DB_SYSTEM, dbSystem)
                .setAttribute(DB_STATEMENT, sanitizeAndMaskSql(sql))
                .startSpan();

        if (connection != null) {
            try {
                if (dbUrl != null) {
                    span.setAttribute(DB_CONNECTION_STRING, sanitizeUrl(dbUrl));
                    span.setAttribute(DB_NAME, dbName);
                }
                String user = connection.getMetaData().getUserName();
                if (user != null && !user.isEmpty()) {
                    span.setAttribute(DB_USER, user);
                }
            } catch (Exception e) {
                span.setAttribute("db.connection.error", e.getMessage());
            }
        }

        if (config != null && config.isCaptureStack()) {
            String stackTrace = captureStackTrace();
            if (stackTrace != null && !stackTrace.isEmpty()) {
                span.setAttribute(CALL_STACK, stackTrace);
            }
        }

        Scope scope = span.makeCurrent();
        long startTime = System.currentTimeMillis();

        try {
            Object result = zuper.call();
            long duration = System.currentTimeMillis() - startTime;

            long rowsReturned = extractRowsReturned(self, methodName, result);
            if (rowsReturned >= 0) {
                span.setAttribute(DB_ROWS_RETURNED, rowsReturned);
            }

            span.setAttribute("db.duration_ms", duration);
            span.setStatus(StatusCode.OK);

            detectSlowSql(sql, dbSystem, connection, duration);

            return result;
        } catch (Exception e) {
            long duration = System.currentTimeMillis() - startTime;
            span.setStatus(StatusCode.ERROR, e.getMessage());
            span.setAttribute("db.error", e.getMessage());
            span.setAttribute("db.error.type", e.getClass().getName());
            span.setAttribute("db.duration_ms", duration);

            detectSlowSql(sql, dbSystem, connection, duration);

            throw e;
        } finally {
            scope.close();
            span.end();
        }
    }

    private static void detectSlowSql(String sql, String dbSystem,
                                      Connection connection, long durationMs) {
        if (config == null || !config.isIndexAdvisorEnabled()) {
            return;
        }
        if (sql == null || connection == null) {
            return;
        }
        try {
            SlowSqlDetector.getInstance().detectAndAnalyze(
                    sql, dbSystem, connection, durationMs);
        } catch (Exception e) {
            // ignore index advisor errors
        }
    }

    private static boolean isExecuteMethod(String methodName) {
        return methodName.equals("execute") ||
                methodName.equals("executeQuery") ||
                methodName.equals("executeUpdate") ||
                methodName.equals("executeBatch") ||
                methodName.equals("executeLargeUpdate");
    }

    private static String extractOperationName(String sql, String methodName) {
        if (sql != null && !sql.isEmpty()) {
            String trimmed = sql.trim().toUpperCase();
            if (trimmed.startsWith("SELECT")) {
                return "SELECT";
            } else if (trimmed.startsWith("INSERT")) {
                return "INSERT";
            } else if (trimmed.startsWith("UPDATE")) {
                return "UPDATE";
            } else if (trimmed.startsWith("DELETE")) {
                return "DELETE";
            } else if (trimmed.startsWith("CREATE")) {
                return "CREATE";
            } else if (trimmed.startsWith("ALTER")) {
                return "ALTER";
            } else if (trimmed.startsWith("DROP")) {
                return "DROP";
            } else if (trimmed.startsWith("TRUNCATE")) {
                return "TRUNCATE";
            }
        }
        return methodName;
    }

    private static String detectDbSystem(Object self) {
        if (self == null) {
            return "unknown";
        }
        String className = self.getClass().getName();
        if (className.contains("mysql")) {
            return "mysql";
        } else if (className.contains("postgresql")) {
            return "postgresql";
        } else if (className.contains("oracle")) {
            return "oracle";
        } else if (className.contains("sqlserver") || className.contains("mssql")) {
            return "sqlserver";
        } else if (className.contains("h2")) {
            return "h2";
        } else if (className.contains("sqlite")) {
            return "sqlite";
        }
        return "other";
    }

    private static String sanitizeAndMaskSql(String sql) {
        if (sql == null) {
            return "";
        }
        String sanitized = sql.replaceAll("\\s+", " ").trim();
        if (config != null && config.isMaskingEnabled()) {
            try {
                SensitiveDataMasker masker = MaskingConfig.getInstance(
                        config.getMaskingConfigPath(),
                        config.getMaskingReloadIntervalMs()).getMasker();
                sanitized = masker.mask(sanitized);
            } catch (Exception e) {
                // ignore masking errors, return sanitized sql as is
            }
        }
        return sanitized;
    }

    private static String sanitizeUrl(String url) {
        if (url == null) {
            return "";
        }
        return url.replaceAll("password=[^&]*", "password=***")
                .replaceAll("pwd=[^&]*", "pwd=***");
    }

    private static String extractDbName(String url) {
        if (url == null) {
            return "";
        }
        try {
            int lastSlash = url.lastIndexOf('/');
            if (lastSlash >= 0 && lastSlash < url.length() - 1) {
                String dbPart = url.substring(lastSlash + 1);
                int queryIndex = dbPart.indexOf('?');
                if (queryIndex > 0) {
                    return dbPart.substring(0, queryIndex);
                }
                return dbPart;
            }
        } catch (Exception e) {
            // ignore
        }
        return "";
    }

    private static long extractRowsReturned(Object self, String methodName, Object result) {
        try {
            if (result instanceof Integer) {
                return (Integer) result;
            }
            if (result instanceof Long) {
                return (Long) result;
            }
            if (result instanceof int[]) {
                int[] results = (int[]) result;
                long total = 0;
                for (int r : results) {
                    if (r > 0) {
                        total += r;
                    }
                }
                return total;
            }
            if (result instanceof java.sql.ResultSet) {
                return -1;
            }
        } catch (Exception e) {
            // ignore
        }
        return -1;
    }

    private static String captureStackTrace() {
        try {
            StackTraceElement[] stackTrace = Thread.currentThread().getStackTrace();
            StringBuilder sb = new StringBuilder();
            int count = 0;
            for (StackTraceElement element : stackTrace) {
                String className = element.getClassName();
                if (className.contains("dbagent") ||
                        className.contains("bytebuddy") ||
                        className.contains("opentelemetry") ||
                        className.contains("java.lang.reflect") ||
                        className.contains("sun.reflect") ||
                        className.contains("$Proxy")) {
                    continue;
                }
                if (count > 0) {
                    sb.append("\n");
                }
                sb.append(className)
                        .append(".")
                        .append(element.getMethodName())
                        .append("(")
                        .append(element.getFileName())
                        .append(":")
                        .append(element.getLineNumber())
                        .append(")");
                count++;
                if (count >= 10) {
                    break;
                }
            }
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }
}
