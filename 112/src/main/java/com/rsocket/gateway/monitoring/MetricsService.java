package com.rsocket.gateway.monitoring;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

import java.time.Duration;
import java.time.Instant;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.LongAdder;

@Slf4j
@Service
public class MetricsService {

    private final Map<String, ServiceMetrics> metricsMap = new ConcurrentHashMap<>();

    public void recordRequest(String serviceName, String methodName, Duration latency, boolean success) {
        ServiceMetrics metrics = metricsMap.computeIfAbsent(serviceName, k -> new ServiceMetrics(serviceName));
        metrics.record(methodName, latency, success);
    }

    public ServiceMetrics getServiceMetrics(String serviceName) {
        return metricsMap.get(serviceName);
    }

    public Map<String, ServiceMetrics> getAllMetrics() {
        return new HashMap<>(metricsMap);
    }

    public DashboardMetrics getDashboardMetrics() {
        long totalRequests = 0;
        long totalSuccess = 0;
        long totalFailures = 0;
        List<Long> allLatencies = new ArrayList<>();

        for (ServiceMetrics metrics : metricsMap.values()) {
            totalRequests += metrics.getTotalRequests();
            totalSuccess += metrics.getSuccessCount();
            totalFailures += metrics.getFailureCount();
            allLatencies.addAll(metrics.getAllLatencies());
        }

        return DashboardMetrics.builder()
                .totalRequests(totalRequests)
                .successCount(totalSuccess)
                .failureCount(totalFailures)
                .qps(calculateQPS(totalRequests))
                .latencyPercentiles(calculatePercentiles(allLatencies))
                .serviceMetrics(new ArrayList<>(metricsMap.values()))
                .build();
    }

    private double calculateQPS(long totalRequests) {
        return totalRequests / 60.0;
    }

    private Map<String, Long> calculatePercentiles(List<Long> latencies) {
        if (latencies.isEmpty()) {
            return Collections.emptyMap();
        }
        Collections.sort(latencies);
        Map<String, Long> percentiles = new HashMap<>();
        percentiles.put("p50", getPercentile(latencies, 50));
        percentiles.put("p75", getPercentile(latencies, 75));
        percentiles.put("p90", getPercentile(latencies, 90));
        percentiles.put("p95", getPercentile(latencies, 95));
        percentiles.put("p99", getPercentile(latencies, 99));
        return percentiles;
    }

    private long getPercentile(List<Long> sortedList, int percentile) {
        int index = (int) Math.ceil(percentile / 100.0 * sortedList.size()) - 1;
        return sortedList.get(Math.max(0, index));
    }

    @Data
    public static class ServiceMetrics {
        private final String serviceName;
        private final LongAdder totalRequests = new LongAdder();
        private final LongAdder successCount = new LongAdder();
        private final LongAdder failureCount = new LongAdder();
        private final Map<String, MethodMetrics> methodMetrics = new ConcurrentHashMap<>();
        private final List<Long> latencies = Collections.synchronizedList(new ArrayList<>());
        private Instant startTime = Instant.now();

        public ServiceMetrics(String serviceName) {
            this.serviceName = serviceName;
        }

        public void record(String methodName, Duration latency, boolean success) {
            totalRequests.increment();
            if (success) {
                successCount.increment();
            } else {
                failureCount.increment();
            }
            latencies.add(latency.toMillis());
            if (latencies.size() > 10000) {
                latencies.remove(0);
            }
            methodMetrics.computeIfAbsent(methodName, k -> new MethodMetrics(methodName))
                    .record(latency, success);
        }

        public long getTotalRequests() {
            return totalRequests.sum();
        }

        public long getSuccessCount() {
            return successCount.sum();
        }

        public long getFailureCount() {
            return failureCount.sum();
        }

        public List<Long> getAllLatencies() {
            return new ArrayList<>(latencies);
        }
    }

    @Data
    public static class MethodMetrics {
        private final String methodName;
        private final LongAdder totalRequests = new LongAdder();
        private final LongAdder successCount = new LongAdder();
        private final LongAdder failureCount = new LongAdder();
        private final List<Long> latencies = Collections.synchronizedList(new ArrayList<>());

        public MethodMetrics(String methodName) {
            this.methodName = methodName;
        }

        public void record(Duration latency, boolean success) {
            totalRequests.increment();
            if (success) {
                successCount.increment();
            } else {
                failureCount.increment();
            }
            latencies.add(latency.toMillis());
            if (latencies.size() > 1000) {
                latencies.remove(0);
            }
        }
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class DashboardMetrics {
        private long totalRequests;
        private long successCount;
        private long failureCount;
        private double qps;
        private Map<String, Long> latencyPercentiles;
        private List<ServiceMetrics> serviceMetrics;
    }
}
