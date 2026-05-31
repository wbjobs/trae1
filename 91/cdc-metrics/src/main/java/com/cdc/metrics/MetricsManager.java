package com.cdc.metrics;

import com.cdc.common.config.MetricsConfig;
import io.prometheus.client.Counter;
import io.prometheus.client.Gauge;
import io.prometheus.client.Histogram;
import io.prometheus.client.exporter.HTTPServer;
import io.prometheus.client.hotspot.DefaultExports;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.net.InetSocketAddress;

public class MetricsManager {

    private static final Logger logger = LoggerFactory.getLogger(MetricsManager.class);

    private final MetricsConfig config;
    private HTTPServer httpServer;

    private static final Counter totalEvents = Counter.build()
            .name("cdc_events_total")
            .help("Total number of CDC events processed")
            .labelNames("database", "table", "operation")
            .register();

    private static final Counter eventsFailed = Counter.build()
            .name("cdc_events_failed_total")
            .help("Total number of failed CDC events")
            .labelNames("database", "table")
            .register();

    private static final Gauge eventLagSeconds = Gauge.build()
            .name("cdc_event_lag_seconds")
            .help("Lag of CDC events in seconds")
            .labelNames("database", "table")
            .register();

    private static final Gauge lastEventTimestamp = Gauge.build()
            .name("cdc_last_event_timestamp_seconds")
            .help("Timestamp of last processed event")
            .labelNames("database", "table")
            .register();

    private static final Gauge snapshotProgress = Gauge.build()
            .name("cdc_snapshot_progress_percent")
            .help("Snapshot progress percentage")
            .labelNames("database", "table")
            .register();

    private static final Histogram eventProcessingLatency = Histogram.build()
            .name("cdc_event_processing_latency_seconds")
            .help("Latency of event processing")
            .labelNames("database", "table")
            .buckets(0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1, 5, 10, 30, 60)
            .register();

    private static final Gauge kafkaProducerQueueSize = Gauge.build()
            .name("cdc_kafka_producer_queue_size")
            .help("Kafka producer queue size")
            .register();

    public MetricsManager(MetricsConfig config) {
        this.config = config;
    }

    public void start() throws IOException {
        if (!config.isEnabled()) {
            logger.info("Metrics are disabled");
            return;
        }

        DefaultExports.initialize();

        InetSocketAddress address = new InetSocketAddress(config.getPort());
        httpServer = new HTTPServer(address, true);
        logger.info("Metrics server started on port {}, path: {}", config.getPort(), config.getPath());
    }

    public void stop() {
        if (httpServer != null) {
            httpServer.stop();
            logger.info("Metrics server stopped");
        }
    }

    public void recordEvent(String database, String table, String operation) {
        totalEvents.labels(database, table, operation).inc();
    }

    public void recordFailedEvent(String database, String table) {
        eventsFailed.labels(database, table).inc();
    }

    public void recordEventLag(String database, String table, long eventTimestampMs) {
        long now = System.currentTimeMillis();
        double lagSeconds = (now - eventTimestampMs) / 1000.0;
        eventLagSeconds.labels(database, table).set(lagSeconds);
        lastEventTimestamp.labels(database, table).set(eventTimestampMs / 1000.0);
    }

    public void recordSnapshotProgress(String database, String table, double percent) {
        snapshotProgress.labels(database, table).set(percent);
    }

    public Histogram.Timer startProcessingTimer(String database, String table) {
        return eventProcessingLatency.labels(database, table).startTimer();
    }

    public void setKafkaProducerQueueSize(int size) {
        kafkaProducerQueueSize.set(size);
    }

    public static Counter getTotalEvents() {
        return totalEvents;
    }

    public static Counter getEventsFailed() {
        return eventsFailed;
    }

    public static Gauge getEventLagSeconds() {
        return eventLagSeconds;
    }

    public static Gauge getLastEventTimestamp() {
        return lastEventTimestamp;
    }

    public static Gauge getSnapshotProgress() {
        return snapshotProgress;
    }

    public static Histogram getEventProcessingLatency() {
        return eventProcessingLatency;
    }
}
