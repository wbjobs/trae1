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
public class AnomalyTrendPoint {

    @JsonFormat(shape = JsonFormat.Shape.STRING)
    private Instant timestamp;

    private long anomalyCount;

    private long highConcurrencyCount;

    private long lowDurationCount;

    private long repetitivePathCount;

    private long crawlerCount;
}
