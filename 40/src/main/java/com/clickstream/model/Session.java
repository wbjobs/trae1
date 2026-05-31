package com.clickstream.model;

import com.fasterxml.jackson.annotation.JsonFormat;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.Instant;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class Session {

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
}
