package com.logservice.model;

public class SearchHit {
    private final long lineId;
    private final long timestamp;
    private final String line;
    private final String[] contextBefore;
    private final String[] contextAfter;

    public SearchHit(long lineId, long timestamp, String line, String[] contextBefore, String[] contextAfter) {
        this.lineId = lineId;
        this.timestamp = timestamp;
        this.line = line;
        this.contextBefore = contextBefore;
        this.contextAfter = contextAfter;
    }

    public long getLineId() { return lineId; }
    public long getTimestamp() { return timestamp; }
    public String getLine() { return line; }
    public String[] getContextBefore() { return contextBefore; }
    public String[] getContextAfter() { return contextAfter; }
}
