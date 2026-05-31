package com.clickstream.processor;

import com.clickstream.config.KafkaStreamsConfig;
import com.clickstream.model.ClickEvent;
import com.clickstream.model.Session;
import com.clickstream.model.SessionAggregate;
import com.clickstream.model.SessionDetail;
import com.clickstream.store.SessionStore;
import org.apache.kafka.common.serialization.Serdes;
import org.apache.kafka.streams.StreamsBuilder;
import org.apache.kafka.streams.kstream.*;
import org.apache.kafka.streams.kstream.internals.TimeWindow;
import org.apache.kafka.streams.state.SessionStore;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import java.time.Duration;

@Component
public class ClickStreamSessionProcessor {

    private static final Logger logger = LoggerFactory.getLogger(ClickStreamSessionProcessor.class);

    @Value("${kafka.input.topic:click-events}")
    private String inputTopic;

    @Value("${kafka.output.topic:sessions}")
    private String outputTopic;

    @Value("${kafka.output.detail.topic:session-details}")
    private String outputDetailTopic;

    @Value("${session.timeout.minutes:30}")
    private long sessionTimeoutMinutes;

    @Value("${session.grace.minutes:1}")
    private long sessionGraceMinutes;

    private final KafkaStreamsConfig.JsonSerde<ClickEvent> clickEventSerde;
    private final KafkaStreamsConfig.JsonSerde<SessionAggregate> sessionAggregateSerde;
    private final KafkaStreamsConfig.JsonSerde<Session> sessionSerde;
    private final KafkaStreamsConfig.JsonSerde<SessionDetail> sessionDetailSerde;
    private final SessionStore sessionStore;

    @Autowired
    public ClickStreamSessionProcessor(
            KafkaStreamsConfig.JsonSerde<ClickEvent> clickEventSerde,
            KafkaStreamsConfig.JsonSerde<SessionAggregate> sessionAggregateSerde,
            KafkaStreamsConfig.JsonSerde<Session> sessionSerde,
            KafkaStreamsConfig.JsonSerde<SessionDetail> sessionDetailSerde,
            SessionStore sessionStore) {
        this.clickEventSerde = clickEventSerde;
        this.sessionAggregateSerde = sessionAggregateSerde;
        this.sessionSerde = sessionSerde;
        this.sessionDetailSerde = sessionDetailSerde;
        this.sessionStore = sessionStore;
    }

    public void buildTopology(StreamsBuilder streamsBuilder) {
        Duration sessionTimeout = Duration.ofMinutes(sessionTimeoutMinutes);
        Duration gracePeriod = Duration.ofMinutes(sessionGraceMinutes);

        logger.info("Configuring session windows: timeout={}min, grace={}min", sessionTimeoutMinutes, sessionGraceMinutes);

        KStream<String, ClickEvent> clickEvents = streamsBuilder
                .stream(inputTopic, Consumed.with(Serdes.String(), clickEventSerde))
                .filter((key, event) -> event != null && event.getUserId() != null && event.getPageUrl() != null)
                .peek((key, event) -> logger.debug("Processing click event: userId={}, url={}, ts={}",
                        event.getUserId(), event.getPageUrl(), event.getTimestamp()));

        clickEvents
                .groupBy((key, event) -> event.getUserId(), Grouped.with(Serdes.String(), clickEventSerde))
                .windowedBy(SessionWindows.ofInactivityGapAndGrace(sessionTimeout, gracePeriod))
                .aggregate(
                        SessionAggregate::new,
                        this::aggregateClick,
                        this::mergeAggregates,
                        Materialized.<String, SessionAggregate, org.apache.kafka.streams.state.SessionStore<org.apache.kafka.common.utils.Bytes, byte[]>>as("session-aggregates")
                                .withKeySerde(Serdes.String())
                                .withValueSerde(sessionAggregateSerde)
                )
                .toStream()
                .filter((windowedKey, aggregate) -> aggregate != null && aggregate.getUserId() != null)
                .peek((windowedKey, aggregate) -> {
                    Session session = aggregate.toSession();
                    logger.info("Session window closed: sessionId={}, userId={}, pages={}, duration={}s",
                            session.getSessionId(), session.getUserId(),
                            session.getPageCount(), session.getDurationSeconds());
                })
                .mapValues(this::aggregateToSession)
                .to(outputTopic, Produced.with(Serdes.String(), sessionSerde));

        clickEvents
                .groupBy((key, event) -> event.getUserId(), Grouped.with(Serdes.String(), clickEventSerde))
                .windowedBy(SessionWindows.ofInactivityGapAndGrace(sessionTimeout, gracePeriod))
                .aggregate(
                        SessionAggregate::new,
                        this::aggregateClick,
                        this::mergeAggregates,
                        Materialized.<String, SessionAggregate, org.apache.kafka.streams.state.SessionStore<org.apache.kafka.common.utils.Bytes, byte[]>>as("session-details-aggregates")
                                .withKeySerde(Serdes.String())
                                .withValueSerde(sessionAggregateSerde)
                )
                .toStream()
                .filter((windowedKey, aggregate) -> aggregate != null && aggregate.getUserId() != null)
                .mapValues(this::aggregateToSessionDetail)
                .peek((windowedKey, detail) -> {
                    if (detail != null) {
                        sessionStore.save(detail);
                        logger.debug("Saved session detail: sessionId={}", detail.getSessionId());
                    }
                })
                .to(outputDetailTopic, Produced.with(Serdes.String(), sessionDetailSerde));
    }

    private SessionAggregate aggregateClick(String userId, ClickEvent click, SessionAggregate aggregate) {
        if (aggregate.getSessionId() == null) {
            return SessionAggregate.createFromClick(click);
        }
        return aggregate.addClick(click);
    }

    private SessionAggregate mergeAggregates(String userId, SessionAggregate agg1, SessionAggregate agg2) {
        if (agg1.getSessionId() == null) {
            return agg2;
        }
        if (agg2.getSessionId() == null) {
            return agg1;
        }

        SessionAggregate merged = SessionAggregate.builder()
                .sessionId(agg1.getSessionId())
                .userId(agg1.getUserId())
                .startTime(agg1.getStartTime().isBefore(agg2.getStartTime()) ? agg1.getStartTime() : agg2.getStartTime())
                .endTime(agg1.getEndTime().isAfter(agg2.getEndTime()) ? agg1.getEndTime() : agg2.getEndTime())
                .userAgent(agg1.getUserAgent() != null ? agg1.getUserAgent() : agg2.getUserAgent())
                .build();

        java.util.List<SessionAggregate.PageView> mergedPageViews = new java.util.ArrayList<>();
        mergedPageViews.addAll(agg1.getPageViews());
        mergedPageViews.addAll(agg2.getPageViews());
        mergedPageViews.sort((a, b) -> a.getTimestamp().compareTo(b.getTimestamp()));
        merged.setPageViews(mergedPageViews);

        return merged;
    }

    private Session aggregateToSession(SessionAggregate aggregate) {
        return aggregate.toSession();
    }

    private SessionDetail aggregateToSessionDetail(SessionAggregate aggregate) {
        return aggregate.toSessionDetail();
    }
}
