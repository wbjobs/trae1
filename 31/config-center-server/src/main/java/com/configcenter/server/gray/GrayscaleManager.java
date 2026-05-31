package com.configcenter.server.gray;

import com.configcenter.server.model.ClientStateEntity;
import com.configcenter.server.model.ConfigKeyEntity;
import com.configcenter.server.model.GrayscaleBatchEntity;
import com.configcenter.server.service.ConfigService;
import com.configcenter.server.store.GrayscaleRepository;
import com.configcenter.server.store.ConfigRepository.CurrentRecord;
import com.configcenter.server.store.ConfigRepository.StoreException;
import com.configcenter.server.watch.WatchManager;
import jakarta.annotation.PreDestroy;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.util.List;
import java.util.UUID;

@Slf4j
@Component
@RequiredArgsConstructor
public class GrayscaleManager {

    public static final long DEFAULT_OBSERVE_SEC = 15 * 60L;
    public static final int DEFAULT_PERCENT = 10;
    public static final int DEFAULT_ERROR_THRESHOLD_PCT = 20;

    private final GrayscaleRepository grayRepo;
    private final ConfigService configService;
    private final WatchManager watchManager;

    public GrayscaleBatchEntity start(ConfigKeyEntity key, String newContent, Integer percent,
                                       Long observeWindowSec, String operator, Integer errorThresholdPct) {
        int pct = percent == null ? DEFAULT_PERCENT : percent;
        if (pct < 1 || pct > 99) throw new StoreException("percent must be 1-99");

        long observe = observeWindowSec == null ? DEFAULT_OBSERVE_SEC : observeWindowSec;
        int threshold = errorThresholdPct == null ? DEFAULT_ERROR_THRESHOLD_PCT : errorThresholdPct;

        GrayscaleBatchEntity existing = grayRepo.getActiveBatch(key);
        if (existing != null) {
            throw new StoreException("there is already an active batch for this config: " + existing.getBatchId());
        }

        CurrentRecord current = configService.getCurrent(key);
        long targetVersion = (current == null ? 1L : current.getVersion() + 1L);

        String batchId = "gray-" + UUID.randomUUID().toString().replace("-", "").substring(0, 10);

        GrayscaleBatchEntity batch = GrayscaleBatchEntity.builder()
                .batchId(batchId)
                .key(key)
                .content(newContent)
                .targetVersion(targetVersion)
                .percent(pct)
                .observeWindowSec(observe)
                .operator(operator == null ? "gray" : operator)
                .createdAt(System.currentTimeMillis())
                .status(GrayscaleBatchEntity.statusActive())
                .errorThresholdPct(threshold)
                .diff(current == null ? "initial creation" : diffSummary(current.getContent(), newContent))
                .build();

        grayRepo.saveBatch(batch);
        grayRepo.setActiveBatch(key, batchId);
        log.info("grayscale batch {} started for {}/{}/{} percent={} observeWindow={}s",
                batchId, key.getApplication(), key.getProfile(), key.getKey(), pct, observe);

        // Immediately push to gray clients.
        pushToGrayClients(batch, "MODIFIED");
        return batch;
    }

    public GrayscaleBatchEntity promote(String batchId, String operator) {
        GrayscaleBatchEntity batch = grayRepo.getBatch(batchId);
        if (batch == null) throw new StoreException("batch not found: " + batchId);
        if (!batch.isActive()) throw new StoreException("batch is not active: " + batch.getStatus());

        CurrentRecord next = configService.publish(batch.getKey(), batch.getContent(),
                operator == null ? batch.getOperator() : operator);

        batch.setStatus(GrayscaleBatchEntity.statusPromoted());
        batch.setResolvedAt(System.currentTimeMillis());
        batch.setResolution("MANUAL_PROMOTE");
        grayRepo.saveBatch(batch);
        grayRepo.clearActiveBatch(batch.getKey());

        // Push to all watchers as a full rollout.
        watchManager.broadcast(batch.getKey(), next, "MODIFIED");

        log.info("grayscale batch {} promoted to v{}", batchId, next.getVersion());
        return batch;
    }

    public GrayscaleBatchEntity cancel(String batchId, String operator) {
        GrayscaleBatchEntity batch = grayRepo.getBatch(batchId);
        if (batch == null) throw new StoreException("batch not found: " + batchId);
        if (!batch.isActive()) throw new StoreException("batch is not active: " + batch.getStatus());

        batch.setStatus(GrayscaleBatchEntity.statusCancelled());
        batch.setResolvedAt(System.currentTimeMillis());
        batch.setResolution("MANUAL_CANCEL:" + (operator == null ? "gray" : operator));
        grayRepo.saveBatch(batch);
        grayRepo.clearActiveBatch(batch.getKey());

        // Roll gray clients back to current by re-pushing current.
        CurrentRecord cur = configService.getCurrent(batch.getKey());
        if (cur != null) {
            watchManager.broadcast(batch.getKey(), cur, "MODIFIED");
        }
        log.info("grayscale batch {} cancelled", batchId);
        return batch;
    }

    // ------------------------------------------------------------------
    //  observer: every 10s evaluate all active batches
    // ------------------------------------------------------------------

    @Scheduled(fixedDelay = 10_000L)
    public void evaluate() {
        try {
            List<GrayscaleBatchEntity> active = grayRepo.listBatches(true);
            for (GrayscaleBatchEntity batch : active) {
                evaluateOne(batch);
            }
        } catch (Exception e) {
            log.warn("grayscale evaluate failed: {}", e.getMessage());
        }
    }

    private void evaluateOne(GrayscaleBatchEntity batch) {
        long now = System.currentTimeMillis();
        long elapsed = (now - batch.getCreatedAt()) / 1000L;
        if (elapsed < batch.getObserveWindowSec()) {
            // Still observing — only auto-cancel if error rate exceeds threshold.
            int[] stats = computeStats(batch);
            int total = stats[0], error = stats[1];
            if (total > 0) {
                double errorRate = (double) error / total * 100.0;
                if (errorRate >= batch.getErrorThresholdPct()) {
                    log.warn("grayscale batch {} error rate {}% exceeds threshold {}%, auto-cancelling",
                            batch.getBatchId(), errorRate, batch.getErrorThresholdPct());
                    batch.setStatus(GrayscaleBatchEntity.statusAutoCancelled());
                    batch.setResolvedAt(now);
                    batch.setResolution("AUTO_CANCEL:errorRate=" + errorRate);
                    grayRepo.saveBatch(batch);
                    grayRepo.clearActiveBatch(batch.getKey());
                    CurrentRecord cur = configService.getCurrent(batch.getKey());
                    if (cur != null) watchManager.broadcast(batch.getKey(), cur, "MODIFIED");
                }
            }
            return;
        }

        // Observation window ended — auto-promote.
        int[] stats = computeStats(batch);
        double errorRate = stats[0] == 0 ? 0.0 : (double) stats[1] / stats[0] * 100.0;
        if (errorRate >= batch.getErrorThresholdPct()) {
            log.warn("grayscale batch {} observation ended but error rate {}% >= threshold, auto-cancelling",
                    batch.getBatchId(), errorRate);
            batch.setStatus(GrayscaleBatchEntity.statusAutoCancelled());
            batch.setResolvedAt(now);
            batch.setResolution("AUTO_CANCEL:errorRate=" + errorRate);
            grayRepo.saveBatch(batch);
            grayRepo.clearActiveBatch(batch.getKey());
            CurrentRecord cur = configService.getCurrent(batch.getKey());
            if (cur != null) watchManager.broadcast(batch.getKey(), cur, "MODIFIED");
            return;
        }

        log.info("grayscale batch {} observe window elapsed, auto-promoting (errorRate={}%)",
                batch.getBatchId(), errorRate);
        CurrentRecord next = configService.publish(batch.getKey(), batch.getContent(), "auto-promote");
        batch.setStatus(GrayscaleBatchEntity.statusAutoPromoted());
        batch.setResolvedAt(now);
        batch.setResolution("AUTO_PROMOTE:errorRate=" + errorRate);
        grayRepo.saveBatch(batch);
        grayRepo.clearActiveBatch(batch.getKey());
        watchManager.broadcast(batch.getKey(), next, "MODIFIED");
    }

    // ------------------------------------------------------------------
    //  status helper
    // ------------------------------------------------------------------

    public GrayscaleStatus getStatus(String batchId) {
        GrayscaleBatchEntity batch = grayRepo.getBatch(batchId);
        if (batch == null) return null;
        int[] stats = computeStats(batch);
        long elapsed = (System.currentTimeMillis() - batch.getCreatedAt()) / 1000L;
        long remaining = Math.max(0L, batch.getObserveWindowSec() - elapsed);
        return new GrayscaleStatus(batch, stats[0], stats[2], stats[3], stats[1], stats[4],
                stats[2] == 0 ? 0.0 : (double) stats[1] / stats[2] * 100.0,
                batch.isActive() ? remaining : 0L);
    }

    /**
     * Returns [totalClients, errorClients, grayClients, healthyClients, upgradedClients]
     * for the key of this batch.
     */
    public int[] computeStats(GrayscaleBatchEntity batch) {
        List<ClientStateEntity> clients = grayRepo.listClients(batch.getKey().getApplication(), batch.getKey().getProfile());
        int total = clients.size();
        int gray = 0, healthy = 0, error = 0, upgraded = 0;
        for (ClientStateEntity c : clients) {
            if (inGraySet(c, batch.getPercent())) {
                gray++;
                if (c.getVersion() >= batch.getTargetVersion()) upgraded++;
                if (!c.isHealthy()) error++;
                else healthy++;
            }
        }
        return new int[]{total, error, gray, healthy, upgraded};
    }

    public boolean isGrayClient(ClientStateEntity client, ConfigKeyEntity key) {
        GrayscaleBatchEntity b = grayRepo.getActiveBatch(key);
        if (b == null) return false;
        return inGraySet(client, b.getPercent());
    }

    public static boolean inGraySet(ClientStateEntity client, int percent) {
        String seed = (client.getClientIp() == null ? "" : client.getClientIp())
                + "#" + (client.getInstanceId() == null ? "" : client.getInstanceId());
        int hash = Math.abs(murmur(seed));
        return (hash % 100) < percent;
    }

    // Murmur-style 32-bit hash (portable, no external deps)
    private static int murmur(String s) {
        int h = 0x9747b28c;
        for (int i = 0; i < s.length(); i++) {
            int k = s.charAt(i);
            k *= 0xcc9e2d51;
            k = (k << 15) | (k >>> 17);
            k *= 0x1b873593;
            h ^= k;
            h = (h << 13) | (h >>> 19);
            h = h * 5 + 0xe6546b64;
        }
        h ^= s.length();
        h ^= h >>> 16;
        h *= 0x85ebca6b;
        h ^= h >>> 13;
        h *= 0xc2b2ae35;
        h ^= h >>> 16;
        return h;
    }

    private void pushToGrayClients(GrayscaleBatchEntity batch, String eventType) {
        List<ClientStateEntity> clients = grayRepo.listClients(
                batch.getKey().getApplication(), batch.getKey().getProfile());
        java.util.Set<String> grayInstanceIds = new java.util.HashSet<>();
        for (ClientStateEntity c : clients) {
            if (inGraySet(c, batch.getPercent())) {
                grayInstanceIds.add(c.getInstanceId());
            }
        }
        if (grayInstanceIds.isEmpty()) {
            log.debug("pushToGrayClients: no gray clients for batch {}", batch.getBatchId());
            return;
        }
        CurrentRecord fake = CurrentRecord.builder()
                .application(batch.getKey().getApplication())
                .profile(batch.getKey().getProfile())
                .key(batch.getKey().getKey())
                .content(batch.getContent())
                .version(batch.getTargetVersion())
                .updatedBy(batch.getOperator())
                .updatedAt(System.currentTimeMillis())
                .build();
        watchManager.broadcastToGray(batch.getKey(), fake, eventType, batch.getBatchId(), grayInstanceIds);
        log.debug("pushToGrayClients: pushed to {} gray clients out of {} for batch {}",
                grayInstanceIds.size(), clients.size(), batch.getBatchId());
    }

    private static String diffSummary(String a, String b) {
        if (a == null) a = "";
        if (b == null) b = "";
        int la = a.split("\n").length;
        int lb = b.split("\n").length;
        return String.format("lines %d -> %d; len %d -> %d", la, lb, a.length(), b.length());
    }

    @PreDestroy
    public void shutdown() {}

    public record GrayscaleStatus(
            GrayscaleBatchEntity batch,
            int totalClients,
            int grayClients,
            int healthyClients,
            int errorClients,
            int upgradedClients,
            double errorRatePct,
            long remainingSec
    ) {}
}
