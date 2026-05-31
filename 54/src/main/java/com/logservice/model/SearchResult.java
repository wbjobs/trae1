package com.logservice.model;

import java.util.List;

public class SearchResult {
    private final List<SearchHit> hits;
    private final boolean hasMore;
    private final Long nextCursor;
    private final boolean truncated;
    private final String truncatedReason;
    private final long elapsedMs;

    public SearchResult(List<SearchHit> hits, boolean hasMore, Long nextCursor,
                         boolean truncated, String truncatedReason, long elapsedMs) {
        this.hits = hits;
        this.hasMore = hasMore;
        this.nextCursor = nextCursor;
        this.truncated = truncated;
        this.truncatedReason = truncatedReason;
        this.elapsedMs = elapsedMs;
    }

    public List<SearchHit> getHits() { return hits; }
    public boolean isHasMore() { return hasMore; }
    public Long getNextCursor() { return nextCursor; }
    public boolean isTruncated() { return truncated; }
    public String getTruncatedReason() { return truncatedReason; }
    public long getElapsedMs() { return elapsedMs; }
}
