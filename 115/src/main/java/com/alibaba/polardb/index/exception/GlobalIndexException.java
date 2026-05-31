package com.alibaba.polardb.index.exception;

public class GlobalIndexException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    private String errorCode;

    public GlobalIndexException(String message) {
        super(message);
    }

    public GlobalIndexException(String errorCode, String message) {
        super(message);
        this.errorCode = errorCode;
    }

    public GlobalIndexException(String message, Throwable cause) {
        super(message, cause);
    }

    public GlobalIndexException(String errorCode, String message, Throwable cause) {
        super(message, cause);
        this.errorCode = errorCode;
    }

    public String getErrorCode() {
        return errorCode;
    }
}
