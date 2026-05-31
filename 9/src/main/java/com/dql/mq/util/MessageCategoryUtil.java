package com.dql.mq.util;

import lombok.extern.slf4j.Slf4j;

@Slf4j
public class MessageCategoryUtil {

    public static final String CATEGORY_BUSINESS = "BUSINESS";
    public static final String CATEGORY_SYSTEM = "SYSTEM";
    public static final String CATEGORY_NETWORK = "NETWORK";
    public static final String CATEGORY_DATA = "DATA";
    public static final String CATEGORY_TIMEOUT = "TIMEOUT";
    public static final String CATEGORY_VALIDATION = "VALIDATION";
    public static final String CATEGORY_DATABASE = "DATABASE";
    public static final String CATEGORY_EXTERNAL = "EXTERNAL";
    public static final String CATEGORY_UNKNOWN = "UNKNOWN";

    public static final String ERROR_TYPE_NULL_POINTER = "NULL_POINTER";
    public static final String ERROR_TYPE_IO = "IO_ERROR";
    public static final String ERROR_TYPE_SQL = "SQL_ERROR";
    public static final String ERROR_TYPE_TIMEOUT = "TIMEOUT";
    public static final String ERROR_TYPE_CONNECTION = "CONNECTION_ERROR";
    public static final String ERROR_TYPE_PARSE = "PARSE_ERROR";
    public static final String ERROR_TYPE_VALIDATION = "VALIDATION_ERROR";
    public static final String ERROR_TYPE_AUTHENTICATION = "AUTHENTICATION_ERROR";
    public static final String ERROR_TYPE_PERMISSION = "PERMISSION_ERROR";
    public static final String ERROR_TYPE_BUSINESS = "BUSINESS_ERROR";
    public static final String ERROR_TYPE_SYSTEM = "SYSTEM_ERROR";

    public static String categorizeMessage(String queueName, String errorMessage, String errorStack) {
        if (queueName == null) {
            return CATEGORY_UNKNOWN;
        }

        if (queueName.contains("order")) {
            return CATEGORY_BUSINESS;
        }
        if (queueName.contains("payment") || queueName.contains("transaction")) {
            return CATEGORY_BUSINESS;
        }
        if (queueName.contains("notification") || queueName.contains("email") || queueName.contains("sms")) {
            return CATEGORY_EXTERNAL;
        }
        if (queueName.contains("sync") || queueName.contains("data")) {
            return CATEGORY_DATA;
        }

        if (errorMessage != null) {
            if (errorMessage.contains("timeout") || errorMessage.contains("Timeout") ||
                    errorMessage.contains("Timed out") || errorMessage.contains("connection timed out")) {
                return CATEGORY_TIMEOUT;
            }
            if (errorMessage.contains("SQL") || errorMessage.contains("sql") ||
                    errorMessage.contains("database") || errorMessage.contains("JDBC") ||
                    errorMessage.contains("Hibernate") || errorMessage.contains("JPA")) {
                return CATEGORY_DATABASE;
            }
            if (errorMessage.contains("Connection") || errorMessage.contains("connection") ||
                    errorMessage.contains("connect") || errorMessage.contains("refused")) {
                return CATEGORY_NETWORK;
            }
            if (errorMessage.contains("NullPointer") || errorMessage.contains("null")) {
                return CATEGORY_SYSTEM;
            }
            if (errorMessage.contains("parse") || errorMessage.contains("Parse") ||
                    errorMessage.contains("JSON") || errorMessage.contains("json") ||
                    errorMessage.contains("XML") || errorMessage.contains("xml")) {
                return CATEGORY_DATA;
            }
            if (errorMessage.contains("validation") || errorMessage.contains("Validation") ||
                    errorMessage.contains("invalid") || errorMessage.contains("Invalid")) {
                return CATEGORY_VALIDATION;
            }
        }

        if (errorStack != null) {
            if (errorStack.contains("java.sql") || errorStack.contains("org.hibernate") ||
                    errorStack.contains("org.springframework.jdbc")) {
                return CATEGORY_DATABASE;
            }
            if (errorStack.contains("java.net") || errorStack.contains("java.io.IOException")) {
                return CATEGORY_NETWORK;
            }
            if (errorStack.contains("java.lang.NullPointerException")) {
                return CATEGORY_SYSTEM;
            }
        }

        return CATEGORY_SYSTEM;
    }

    public static String determineErrorType(String errorMessage, String errorStack) {
        if (errorMessage != null) {
            if (errorMessage.contains("NullPointer") || errorMessage.contains("null pointer")) {
                return ERROR_TYPE_NULL_POINTER;
            }
            if (errorMessage.contains("timeout") || errorMessage.contains("Timeout") ||
                    errorMessage.contains("Timed out")) {
                return ERROR_TYPE_TIMEOUT;
            }
            if (errorMessage.contains("SQL") || errorMessage.contains("sql") ||
                    errorMessage.contains("Syntax") || errorMessage.contains("syntax")) {
                return ERROR_TYPE_SQL;
            }
            if (errorMessage.contains("Connection") || errorMessage.contains("connection") ||
                    errorMessage.contains("refused")) {
                return ERROR_TYPE_CONNECTION;
            }
            if (errorMessage.contains("parse") || errorMessage.contains("Parse") ||
                    errorMessage.contains("JSON") || errorMessage.contains("json")) {
                return ERROR_TYPE_PARSE;
            }
            if (errorMessage.contains("validation") || errorMessage.contains("Validation") ||
                    errorMessage.contains("invalid") || errorMessage.contains("Invalid")) {
                return ERROR_TYPE_VALIDATION;
            }
            if (errorMessage.contains("Authentication") || errorMessage.contains("authentication") ||
                    errorMessage.contains("Unauthorized")) {
                return ERROR_TYPE_AUTHENTICATION;
            }
            if (errorMessage.contains("Permission") || errorMessage.contains("permission") ||
                    errorMessage.contains("AccessDenied")) {
                return ERROR_TYPE_PERMISSION;
            }
            if (errorMessage.contains("IO") || errorMessage.contains("I/O") ||
                    errorMessage.contains("IOException")) {
                return ERROR_TYPE_IO;
            }
        }

        if (errorStack != null) {
            if (errorStack.contains("java.lang.NullPointerException")) {
                return ERROR_TYPE_NULL_POINTER;
            }
            if (errorStack.contains("java.sql.SQLException")) {
                return ERROR_TYPE_SQL;
            }
            if (errorStack.contains("java.net.SocketTimeoutException") ||
                    errorStack.contains("java.util.concurrent.TimeoutException")) {
                return ERROR_TYPE_TIMEOUT;
            }
            if (errorStack.contains("java.net.ConnectException") ||
                    errorStack.contains("java.net.SocketException")) {
                return ERROR_TYPE_CONNECTION;
            }
            if (errorStack.contains("com.fasterxml.jackson") || errorStack.contains("com.google.gson")) {
                return ERROR_TYPE_PARSE;
            }
            if (errorStack.contains("javax.validation") || errorStack.contains("jakarta.validation")) {
                return ERROR_TYPE_VALIDATION;
            }
            if (errorStack.contains("java.io.IOException")) {
                return ERROR_TYPE_IO;
            }
        }

        return ERROR_TYPE_SYSTEM;
    }

    public static String determineErrorCode(String errorType, String errorMessage) {
        if (errorType == null) {
            return "UNKNOWN_001";
        }

        switch (errorType) {
            case ERROR_TYPE_NULL_POINTER:
                return "NPE_001";
            case ERROR_TYPE_TIMEOUT:
                return "TIMEOUT_001";
            case ERROR_TYPE_SQL:
                return determineSqlErrorCode(errorMessage);
            case ERROR_TYPE_CONNECTION:
                return "CONN_001";
            case ERROR_TYPE_PARSE:
                return "PARSE_001";
            case ERROR_TYPE_VALIDATION:
                return "VALID_001";
            case ERROR_TYPE_AUTHENTICATION:
                return "AUTH_001";
            case ERROR_TYPE_PERMISSION:
                return "PERM_001";
            case ERROR_TYPE_IO:
                return "IO_001";
            case ERROR_TYPE_BUSINESS:
                return "BIZ_001";
            default:
                return "SYS_001";
        }
    }

    private static String determineSqlErrorCode(String errorMessage) {
        if (errorMessage == null) {
            return "SQL_001";
        }
        if (errorMessage.contains("duplicate") || errorMessage.contains("Duplicate") ||
                errorMessage.contains("constraint") || errorMessage.contains("Constraint")) {
            return "SQL_002";
        }
        if (errorMessage.contains("timeout") || errorMessage.contains("Timeout")) {
            return "SQL_003";
        }
        if (errorMessage.contains("deadlock") || errorMessage.contains("Deadlock")) {
            return "SQL_004";
        }
        return "SQL_001";
    }

    public static boolean isRetriableError(String errorType, String category) {
        if (errorType == null) {
            return true;
        }

        switch (errorType) {
            case ERROR_TYPE_TIMEOUT:
            case ERROR_TYPE_CONNECTION:
            case ERROR_TYPE_IO:
                return true;
            case ERROR_TYPE_NULL_POINTER:
            case ERROR_TYPE_PARSE:
            case ERROR_TYPE_VALIDATION:
            case ERROR_TYPE_AUTHENTICATION:
            case ERROR_TYPE_PERMISSION:
                return false;
            case ERROR_TYPE_SQL:
                return category != null && !category.equals(CATEGORY_DATA);
            default:
                return true;
        }
    }

    public static boolean isBusinessError(String errorType, String category) {
        return CATEGORY_BUSINESS.equals(category) || ERROR_TYPE_BUSINESS.equals(errorType);
    }
}
