package com.configcenter.client;

import com.configcenter.proto.*;
import io.grpc.ManagedChannel;
import io.grpc.ManagedChannelBuilder;
import io.grpc.stub.StreamObserver;
import lombok.extern.slf4j.Slf4j;

import java.io.Closeable;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Enumeration;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;

@Slf4j
public class ConfigCenterClient implements Closeable {

    private static final long INITIAL_BACKOFF_MS = 1_000L;
    private static final long MAX_BACKOFF_MS = 60_000L;
    private static final double BACKOFF_FACTOR = 2.0;
    private static final long KEEPALIVE_TIME_MS = 30_000L;
    private static final long KEEPALIVE_TIMEOUT_MS = 10_000L;
    private static final int BLOCKING_RETRY_COUNT = 3;
    private static final long BLOCKING_RETRY_INTERVAL_MS = 2_000L;
    private static final long HEARTBEAT_INTERVAL_SEC = 30;

    private final String host;
    private final int port;
    private final String instanceId;
    private final String clientIp;
    private final String application;
    private final String profile;

    private final Map<String, ConfigEntry> cache = new ConcurrentHashMap<>();
    private final Map<String, AtomicLong> versionsByKey = new ConcurrentHashMap<>();
    private final List<WatchSession> activeSessions = new CopyOnWriteArrayList<>();
    private final Object blockingChannelLock = new Object();

    private ManagedChannel blockingChannel;
    private ConfigServiceGrpc.ConfigServiceBlockingStub blockingStub;
    private ConfigServiceGrpc.ConfigServiceStub asyncStub;
    private ScheduledExecutorService heartbeatScheduler;
    private ScheduledFuture<?> heartbeatFuture;
    private volatile boolean closed = false;

    // ------------------------------------------------------------------
    //  public builder
    // ------------------------------------------------------------------

    public static Builder builder() { return new Builder(); }

    public static class Builder {
        private String host = "localhost";
        private int port = 9090;
        private String instanceId;
        private String clientIp;
        private String application = "unknown";
        private String profile = "dev";

        public Builder host(String host) { this.host = host; return this; }
        public Builder port(int port) { this.port = port; return this; }
        public Builder instanceId(String id) { this.instanceId = id; return this; }
        public Builder clientIp(String ip) { this.clientIp = ip; return this; }
        public Builder application(String app) { this.application = app; return this; }
        public Builder profile(String profile) { this.profile = profile; return this; }

        public ConfigCenterClient build() { return new ConfigCenterClient(this); }
    }

    private ConfigCenterClient(Builder b) {
        this.host = b.host;
        this.port = b.port;
        this.instanceId = b.instanceId != null ? b.instanceId : UUID.randomUUID().toString();
        this.clientIp = b.clientIp != null ? b.clientIp : detectLocalIp();
        this.application = b.application;
        this.profile = b.profile;

        this.blockingChannel = newChannel();
        this.blockingStub = ConfigServiceGrpc.newBlockingStub(blockingChannel);
        this.asyncStub = ConfigServiceGrpc.newStub(blockingChannel);

        startHeartbeat();
    }

    /** Simple constructor for backward compatibility (no heartbeat, no identity). */
    public ConfigCenterClient(String host, int port) {
        this(builder().host(host).port(port));
    }

    public ConfigCenterClient(ManagedChannel channel) {
        this.host = "unknown";
        this.port = 0;
        this.instanceId = UUID.randomUUID().toString();
        this.clientIp = detectLocalIp();
        this.application = "unknown";
        this.profile = "dev";
        this.blockingChannel = channel;
        this.blockingStub = ConfigServiceGrpc.newBlockingStub(channel);
        this.asyncStub = ConfigServiceGrpc.newStub(channel);
    }

    public String getInstanceId() { return instanceId; }
    public String getClientIp() { return clientIp; }

    // ------------------------------------------------------------------
    //  helpers
    // ------------------------------------------------------------------

    private ManagedChannel newChannel() {
        return ManagedChannelBuilder.forAddress(host, port)
                .usePlaintext()
                .keepAliveTime(KEEPALIVE_TIME_MS, TimeUnit.MILLISECONDS)
                .keepAliveTimeout(KEEPALIVE_TIMEOUT_MS, TimeUnit.MILLISECONDS)
                .keepAliveWithoutCalls(true)
                .build();
    }

    private static String detectLocalIp() {
        try {
            Enumeration<NetworkInterface> nis = NetworkInterface.getNetworkInterfaces();
            while (nis.hasMoreElements()) {
                NetworkInterface ni = nis.nextElement();
                if (ni.isLoopback()) continue;
                Enumeration<InetAddress> addrs = ni.getInetAddresses();
                while (addrs.hasMoreElements()) {
                    InetAddress a = addrs.nextElement();
                    if (!a.isLoopbackAddress() && a.isSiteLocalAddress()) return a.getHostAddress();
                }
            }
            return InetAddress.getLocalHost().getHostAddress();
        } catch (Exception e) {
            return "127.0.0.1";
        }
    }

    private void recreateBlockingChannel() {
        synchronized (blockingChannelLock) {
            if (blockingChannel != null && !blockingChannel.isShutdown()) {
                try { blockingChannel.shutdownNow(); } catch (Exception ignored) {}
            }
            blockingChannel = newChannel();
            blockingStub = ConfigServiceGrpc.newBlockingStub(blockingChannel);
            asyncStub = ConfigServiceGrpc.newStub(blockingChannel);
        }
    }

    // ------------------------------------------------------------------
    //  heartbeat
    // ------------------------------------------------------------------

    private void startHeartbeat() {
        heartbeatScheduler = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "config-heartbeat-" + instanceId.substring(0, 8));
            t.setDaemon(true);
            return t;
        });
        heartbeatFuture = heartbeatScheduler.scheduleAtFixedRate(this::sendHeartbeat,
                HEARTBEAT_INTERVAL_SEC, HEARTBEAT_INTERVAL_SEC, TimeUnit.SECONDS);
    }

    private void sendHeartbeat() {
        if (closed) return;
        try {
            long version = versionsByKey.values().stream()
                    .mapToLong(AtomicLong::get)
                    .max().orElse(0L);
            ClientHeartbeat hb = ClientHeartbeat.newBuilder()
                    .setInstanceId(instanceId)
                    .setClientIp(clientIp)
                    .setApplication(application)
                    .setProfile(profile)
                    .setVersion(version)
                    .setHealthy(true)
                    .setReportedAt(System.currentTimeMillis())
                    .build();
            ClientStateAck ack = executeWithRetry(() ->
                    blockingStub.reportClientState(hb));
            if (!ack.getOk()) log.debug("heartbeat not ok: {}", ack.getMessage());
        } catch (Exception e) {
            log.debug("heartbeat failed: {}", e.getMessage());
        }
    }

    public void reportError(String key, String errorMessage) {
        try {
            long version = versionsByKey.getOrDefault(key, new AtomicLong(0)).get();
            ClientHeartbeat hb = ClientHeartbeat.newBuilder()
                    .setInstanceId(instanceId)
                    .setClientIp(clientIp)
                    .setApplication(application)
                    .setProfile(profile)
                    .setVersion(version)
                    .setHealthy(false)
                    .setErrorMessage(errorMessage == null ? "" : errorMessage)
                    .setReportedAt(System.currentTimeMillis())
                    .build();
            blockingStub.reportClientState(hb);
        } catch (Exception e) {
            log.debug("reportError failed: {}", e.getMessage());
        }
    }

    // ------------------------------------------------------------------
    //  blocking API
    // ------------------------------------------------------------------

    public ConfigEntry getConfig(String application, String profile, String key) {
        return executeWithRetry(() -> {
            GetConfigResponse resp = blockingStub.getConfig(GetConfigRequest.newBuilder()
                    .setConfigKey(buildKey(application, profile, key))
                    .build());
            return resp.getEntry();
        });
    }

    public ConfigEntry publish(String application, String profile, String key,
                               String content, String operator) {
        return executeWithRetry(() -> {
            PublishConfigResponse resp = blockingStub.publishConfig(PublishConfigRequest.newBuilder()
                    .setConfigKey(buildKey(application, profile, key))
                    .setContent(content)
                    .setOperator(operator == null ? "" : operator)
                    .build());
            return resp.getEntry();
        });
    }

    public List<HistoryEntry> getHistory(String application, String profile, String key, int limit) {
        return executeWithRetry(() -> {
            HistoryResponse resp = blockingStub.getHistory(HistoryRequest.newBuilder()
                    .setConfigKey(buildKey(application, profile, key))
                    .setLimit(limit)
                    .build());
            return resp.getEntriesList();
        });
    }

    public ConfigEntry rollback(String application, String profile, String key,
                                long targetVersion, String operator) {
        return executeWithRetry(() -> {
            RollbackResponse resp = blockingStub.rollback(RollbackRequest.newBuilder()
                    .setConfigKey(buildKey(application, profile, key))
                    .setTargetVersion(targetVersion)
                    .setOperator(operator == null ? "" : operator)
                    .build());
            return resp.getEntry();
        });
    }

    public List<String> listApps() {
        return executeWithRetry(() ->
                blockingStub.listApps(ListAppsRequest.newBuilder().build())
                        .getApplicationsList());
    }

    public List<String> listProfiles(String app) {
        return executeWithRetry(() ->
                blockingStub.listProfiles(ListProfilesRequest.newBuilder()
                                .setApplication(app).build())
                        .getProfilesList());
    }

    public List<String> listKeys(String app, String profile) {
        return executeWithRetry(() ->
                blockingStub.listKeys(ListKeysRequest.newBuilder()
                                .setApplication(app).setProfile(profile).build())
                        .getKeysList());
    }

    // ------------------------------------------------------------------
    //  grayscale blocking API
    // ------------------------------------------------------------------

    public GrayscaleBatch startGrayscale(String application, String profile, String key,
                                         String content, int percent,
                                         long observeWindowSec, String operator,
                                         int errorThresholdPct) {
        return executeWithRetry(() -> {
            StartGrayscaleResponse resp = blockingStub.startGrayscale(StartGrayscaleRequest.newBuilder()
                    .setConfigKey(buildKey(application, profile, key))
                    .setContent(content)
                    .setPercent(percent)
                    .setObserveWindowSec(observeWindowSec)
                    .setOperator(operator == null ? "" : operator)
                    .setErrorThresholdPct(errorThresholdPct)
                    .build());
            return resp.getBatch();
        });
    }

    public GrayscaleBatch cancelGrayscale(String batchId, String operator) {
        return executeWithRetry(() -> {
            CancelGrayscaleResponse resp = blockingStub.cancelGrayscale(CancelGrayscaleRequest.newBuilder()
                    .setBatchId(batchId)
                    .setOperator(operator == null ? "" : operator)
                    .build());
            return resp.getBatch();
        });
    }

    public GrayscaleBatch promoteGrayscale(String batchId, String operator) {
        return executeWithRetry(() -> {
            PromoteGrayscaleResponse resp = blockingStub.promoteGrayscale(PromoteGrayscaleRequest.newBuilder()
                    .setBatchId(batchId)
                    .setOperator(operator == null ? "" : operator)
                    .build());
            return resp.getBatch();
        });
    }

    public GetGrayscaleStatusResponse getGrayscaleStatus(String batchId) {
        return executeWithRetry(() ->
                blockingStub.getGrayscaleStatus(GetGrayscaleStatusRequest.newBuilder()
                        .setBatchId(batchId).build()));
    }

    public List<GrayscaleBatch> listGrayscaleBatches(boolean onlyActive) {
        return executeWithRetry(() ->
                blockingStub.listGrayscaleBatches(ListGrayscaleBatchesRequest.newBuilder()
                        .setOnlyActive(onlyActive).build())
                        .getBatchesList());
    }

    public List<ClientView> listClients(String application, String profile) {
        return executeWithRetry(() ->
                blockingStub.listClients(ListClientsRequest.newBuilder()
                        .setApplication(application == null ? "" : application)
                        .setProfile(profile == null ? "" : profile)
                        .build())
                        .getClientsList());
    }

    // ------------------------------------------------------------------
    //  retry helper
    // ------------------------------------------------------------------

    private interface RetryableSupplier<T> { T get() throws Exception; }

    private <T> T executeWithRetry(RetryableSupplier<T> supplier) {
        Exception lastError = null;
        for (int attempt = 0; attempt <= BLOCKING_RETRY_COUNT; attempt++) {
            try {
                return supplier.get();
            } catch (Exception e) {
                lastError = e;
                log.debug("blocking call failed (attempt {}): {}", attempt + 1, e.getMessage());
                if (attempt < BLOCKING_RETRY_COUNT) {
                    try { Thread.sleep(BLOCKING_RETRY_INTERVAL_MS); } catch (InterruptedException ignored) {}
                    recreateBlockingChannel();
                }
            }
        }
        throw new RuntimeException("blocking call failed after " + (BLOCKING_RETRY_COUNT + 1) + " attempts", lastError);
    }

    private static ConfigKey buildKey(String application, String profile, String key) {
        return ConfigKey.newBuilder()
                .setApplication(application)
                .setProfile(profile)
                .setKey(key == null ? "" : key)
                .build();
    }

    // ------------------------------------------------------------------
    //  watch API
    // ------------------------------------------------------------------

    public WatchHandle watch(String application, String profile, String key,
                              long sinceVersion, Consumer<WatchConfigResponse> consumer) {
        if (closed) throw new IllegalStateException("client already closed");
        WatchSession session = new WatchSession(application, profile, key, sinceVersion, consumer);
        activeSessions.add(session);
        session.start();
        return session::cancel;
    }

    public WatchHandle watch(String application, String profile, String key,
                              Consumer<WatchConfigResponse> consumer) {
        long sinceVersion = 0L;
        try {
            ConfigEntry current = getConfig(application, profile, key);
            if (current != null) sinceVersion = current.getVersion();
        } catch (Exception e) {
            log.debug("could not prefetch current version: {}", e.getMessage());
        }
        return watch(application, profile, key, sinceVersion, consumer);
    }

    // ------------------------------------------------------------------
    //  cache
    // ------------------------------------------------------------------

    public ConfigEntry getCached(String application, String profile, String key) {
        return cache.get(cacheKey(application, profile, key));
    }

    private static String cacheKey(ConfigKey k) {
        return cacheKey(k.getApplication(), k.getProfile(), k.getKey());
    }

    private static String cacheKey(String application, String profile, String key) {
        return application + "/" + profile + "/" + (key == null ? "" : key);
    }

    // ------------------------------------------------------------------
    //  lifecycle
    // ------------------------------------------------------------------

    @Override
    public void close() {
        closed = true;
        if (heartbeatFuture != null) heartbeatFuture.cancel(false);
        if (heartbeatScheduler != null) heartbeatScheduler.shutdownNow();
        for (WatchSession s : activeSessions) s.cancel();
        activeSessions.clear();
        if (blockingChannel != null && !blockingChannel.isShutdown()) {
            try { blockingChannel.shutdownNow().awaitTermination(2, TimeUnit.SECONDS); }
            catch (Exception ignored) {}
        }
    }

    // ------------------------------------------------------------------
    //  WatchSession
    // ------------------------------------------------------------------

    private final class WatchSession {
        private final String application;
        private final String profile;
        private final String key;
        private final Consumer<WatchConfigResponse> consumer;

        private volatile long lastVersion;
        private volatile long backoffMs = INITIAL_BACKOFF_MS;
        private volatile boolean cancelled = false;

        private ManagedChannel streamChannel;
        private ScheduledExecutorService scheduler;
        private ScheduledFuture<?> pendingReconnect;
        private final Object sessionLock = new Object();
        private static final AtomicInteger THREAD_COUNTER = new AtomicInteger(1);

        WatchSession(String application, String profile, String key,
                     long sinceVersion, Consumer<WatchConfigResponse> consumer) {
            this.application = application;
            this.profile = profile;
            this.key = key;
            this.lastVersion = sinceVersion;
            this.consumer = consumer;
        }

        void start() {
            scheduler = Executors.newSingleThreadScheduledExecutor(r -> {
                Thread t = new Thread(r, "config-watcher-" + application + "/" + profile + "/" + key
                        + "-" + THREAD_COUNTER.getAndIncrement());
                t.setDaemon(true);
                return t;
            });
            scheduler.submit(this::connectAndWatch);
        }

        void cancel() {
            cancelled = true;
            synchronized (sessionLock) {
                if (pendingReconnect != null) {
                    pendingReconnect.cancel(false);
                    pendingReconnect = null;
                }
            }
            if (scheduler != null && !scheduler.isShutdown()) scheduler.shutdownNow();
            closeStreamChannel();
            activeSessions.remove(this);
        }

        private void connectAndWatch() {
            if (cancelled) return;

            try {
                closeStreamChannel();
                streamChannel = newChannel();
                ConfigServiceGrpc.ConfigServiceStub asyncStub = ConfigServiceGrpc.newStub(streamChannel);

                WatchConfigRequest req = WatchConfigRequest.newBuilder()
                        .setConfigKey(buildKey(application, profile, key))
                        .setSinceVersion(lastVersion)
                        .setInstanceId(instanceId)
                        .setClientIp(clientIp)
                        .build();

                log.info("watch starting: app={} profile={} key={} sinceVersion={} instance={}",
                        application, profile, key, lastVersion, instanceId);

                asyncStub.watchConfig(req, new StreamObserver<WatchConfigResponse>() {
                    @Override
                    public void onNext(WatchConfigResponse resp) {
                        if (cancelled) return;
                        try {
                            ConfigEntry e = resp.getEntry();
                            lastVersion = e.getVersion();
                            cache.put(cacheKey(e.getConfigKey()), e);
                            versionsByKey.computeIfAbsent(cacheKey(e.getConfigKey()),
                                    k -> new AtomicLong(0)).set(e.getVersion());
                            consumer.accept(resp);
                            backoffMs = INITIAL_BACKOFF_MS;
                            log.debug("watch received v{} gray={} batch={}",
                                    e.getVersion(), resp.getGrayscale(), resp.getBatchId());
                        } catch (Exception ex) {
                            log.error("watch consumer error", ex);
                        }
                    }

                    @Override
                    public void onError(Throwable t) {
                        if (cancelled) return;
                        log.warn("watch stream error ({}): {}", describe(), t.getMessage());
                        scheduleReconnect();
                    }

                    @Override
                    public void onCompleted() {
                        if (cancelled) return;
                        log.info("watch stream completed ({}), reconnecting", describe());
                        scheduleReconnect();
                    }
                });
            } catch (Exception e) {
                if (cancelled) return;
                log.warn("watch connect failed ({}): {}", describe(), e.getMessage());
                scheduleReconnect();
            }
        }

        private void scheduleReconnect() {
            if (cancelled) return;
            synchronized (sessionLock) {
                if (cancelled) return;
                long delay = backoffMs;
                backoffMs = Math.min((long) (backoffMs * BACKOFF_FACTOR), MAX_BACKOFF_MS);
                log.info("scheduling watch reconnect for {} in {} ms", describe(), delay);
                pendingReconnect = scheduler.schedule(() -> {
                    synchronized (sessionLock) { pendingReconnect = null; }
                    connectAndWatch();
                }, delay, TimeUnit.MILLISECONDS);
            }
        }

        private void closeStreamChannel() {
            if (streamChannel != null && !streamChannel.isShutdown()) {
                try { streamChannel.shutdownNow().awaitTermination(1, TimeUnit.SECONDS); }
                catch (Exception ignored) {}
            }
            streamChannel = null;
        }

        private String describe() {
            return application + "/" + profile + "/" + key;
        }
    }

    @FunctionalInterface
    public interface WatchHandle extends Closeable {
        void cancel();
        @Override default void close() { cancel(); }
    }
}
