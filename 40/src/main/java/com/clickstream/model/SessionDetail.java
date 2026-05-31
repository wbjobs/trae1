package com.clickstream.model;

import com.fasterxml.jackson.annotation.JsonFormat;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.Instant;
import java.util.List;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class SessionDetail {

    private String sessionId;

    private String userId;

    @JsonFormat(shape = JsonFormat.Shape.STRING)
    private Instant startTime;

    @JsonFormat(shape = JsonFormat.Shape.STRING)
    private Instant endTime;

    private long durationSeconds;

    private int pageCount;

    private boolean bounce;

    private String userAgent;

    private List<PageView> pageSequence;

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class PageView {

        private String pageUrl;

        @JsonFormat(shape = JsonFormat.Shape.STRING)
        private Instant timestamp;

        private String referer;
    }
}
