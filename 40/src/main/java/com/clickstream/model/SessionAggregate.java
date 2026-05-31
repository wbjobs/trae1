package com.clickstream.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class SessionAggregate {

    private String sessionId;

    private String userId;

    private Instant startTime;

    private Instant endTime;

    private List<PageView> pageViews;

    private String userAgent;

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class PageView {

        private String pageUrl;

        private Instant timestamp;

        private String referer;
    }

    public static SessionAggregate createFromClick(ClickEvent click) {
        SessionAggregate aggregate = new SessionAggregate();
        aggregate.setSessionId(UUID.randomUUID().toString());
        aggregate.setUserId(click.getUserId());
        aggregate.setStartTime(click.getTimestamp());
        aggregate.setEndTime(click.getTimestamp());
        aggregate.setUserAgent(click.getUserAgent());

        PageView pageView = PageView.builder()
                .pageUrl(click.getPageUrl())
                .timestamp(click.getTimestamp())
                .referer(click.getReferer())
                .build();

        aggregate.setPageViews(new ArrayList<>());
        aggregate.getPageViews().add(pageView);

        return aggregate;
    }

    public SessionAggregate addClick(ClickEvent click) {
        this.endTime = click.getTimestamp();

        PageView pageView = PageView.builder()
                .pageUrl(click.getPageUrl())
                .timestamp(click.getTimestamp())
                .referer(click.getReferer())
                .build();

        this.pageViews.add(pageView);

        return this;
    }

    public Session toSession() {
        long duration = java.time.Duration.between(startTime, endTime).getSeconds();
        int pageCount = pageViews.size();

        return Session.builder()
                .sessionId(sessionId)
                .userId(userId)
                .startTime(startTime)
                .endTime(endTime)
                .durationSeconds(duration)
                .pageCount(pageCount)
                .bounce(pageCount == 1)
                .userAgent(userAgent)
                .build();
    }

    public SessionDetail toSessionDetail() {
        Session session = toSession();

        List<SessionDetail.PageView> pageViewList = pageViews.stream()
                .map(pv -> SessionDetail.PageView.builder()
                        .pageUrl(pv.getPageUrl())
                        .timestamp(pv.getTimestamp())
                        .referer(pv.getReferer())
                        .build())
                .toList();

        return SessionDetail.builder()
                .sessionId(session.getSessionId())
                .userId(session.getUserId())
                .startTime(session.getStartTime())
                .endTime(session.getEndTime())
                .durationSeconds(session.getDurationSeconds())
                .pageCount(session.getPageCount())
                .bounce(session.isBounce())
                .userAgent(session.getUserAgent())
                .pageSequence(pageViewList)
                .build();
    }
}
