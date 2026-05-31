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
public class ClickEvent {

    private String userId;

    private String pageUrl;

    @JsonFormat(shape = JsonFormat.Shape.STRING)
    private Instant timestamp;

    private String referer;

    private String userAgent;
}
