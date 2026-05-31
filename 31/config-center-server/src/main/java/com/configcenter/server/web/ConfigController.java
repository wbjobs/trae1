package com.configcenter.server.web;

import com.configcenter.server.gray.GrayscaleManager;
import com.configcenter.server.gray.GrayscaleManager.GrayscaleStatus;
import com.configcenter.server.model.ClientStateEntity;
import com.configcenter.server.model.ConfigHistoryEntity;
import com.configcenter.server.model.ConfigKeyEntity;
import com.configcenter.server.model.GrayscaleBatchEntity;
import com.configcenter.server.service.ConfigService;
import com.configcenter.server.store.ConfigRepository.CurrentRecord;
import com.configcenter.server.store.ConfigRepository.StoreException;
import com.configcenter.server.store.GrayscaleRepository;
import com.configcenter.server.web.dto.ApiResponse;
import com.configcenter.server.web.dto.ConfigView;
import com.configcenter.server.web.dto.ConfigView.*;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.http.HttpStatus;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@Slf4j
@RestController
@RequestMapping("/api/v1")
@RequiredArgsConstructor
@CrossOrigin(origins = "*")
public class ConfigController {

    private final ConfigService service;
    private final GrayscaleRepository grayRepo;
    private final GrayscaleManager grayManager;

    // ------------------- list -------------------

    @GetMapping("/apps")
    public ApiResponse<List<String>> listApps() {
        return ApiResponse.ok(service.listApplications());
    }

    @GetMapping("/apps/{app}/profiles")
    public ApiResponse<List<String>> listProfiles(@PathVariable("app") String app) {
        return ApiResponse.ok(service.listProfiles(app));
    }

    @GetMapping("/apps/{app}/profiles/{profile}/keys")
    public ApiResponse<List<KeySummary>> listKeys(@PathVariable("app") String app,
                                                  @PathVariable("profile") String profile) {
        List<com.configcenter.server.model.ConfigKeyEntity> keys = service.listKeys(app, profile);
        List<KeySummary> summaries = keys.stream()
                .map(k -> {
                    CurrentRecord cur = service.getCurrent(k);
                    return KeySummary.builder()
                            .application(k.getApplication())
                            .profile(k.getProfile())
                            .key(k.getKey())
                            .version(cur == null ? 0L : cur.getVersion())
                            .updatedBy(cur == null ? "" : cur.getUpdatedBy())
                            .updatedAt(cur == null ? 0L : cur.getUpdatedAt())
                            .build();
                })
                .toList();
        return ApiResponse.ok(summaries);
    }

    // ------------------- CRUD -------------------

    @GetMapping("/config")
    public ApiResponse<ConfigView> get(@RequestParam("application") String app,
                                       @RequestParam("profile") String profile,
                                       @RequestParam(value = "key", defaultValue = "") String key) {
        ConfigKeyEntity k = ConfigKeyEntity.of(app, profile, key);
        CurrentRecord cur = service.getCurrent(k);
        if (cur == null) {
            return ApiResponse.ok(null);
        }
        return ApiResponse.ok(toView(k, cur));
    }

    @PostMapping("/config")
    public ApiResponse<ConfigView> publish(@RequestBody PublishRequest req) {
        ConfigKeyEntity k = ConfigKeyEntity.of(req.getApplication(), req.getProfile(), req.getKey());
        String operator = (req.getOperator() == null || req.getOperator().isBlank()) ? "web" : req.getOperator();
        CurrentRecord next = service.publish(k, req.getContent(), operator);
        return ApiResponse.ok(toView(k, next));
    }

    @DeleteMapping("/config")
    public ApiResponse<Void> delete(@RequestParam("application") String app,
                                    @RequestParam("profile") String profile,
                                    @RequestParam(value = "key", defaultValue = "") String key,
                                    @RequestParam(value = "operator", defaultValue = "web") String operator) {
        service.delete(ConfigKeyEntity.of(app, profile, key), operator);
        return ApiResponse.ok(null);
    }

    // ------------------- history / rollback -------------------

    @GetMapping("/config/history")
    public ApiResponse<List<HistoryItem>> history(@RequestParam("application") String app,
                                                  @RequestParam("profile") String profile,
                                                  @RequestParam(value = "key", defaultValue = "") String key,
                                                  @RequestParam(value = "limit", defaultValue = "0") int limit) {
        ConfigKeyEntity k = ConfigKeyEntity.of(app, profile, key);
        List<ConfigHistoryEntity> list = service.getHistory(k, limit);
        List<HistoryItem> items = list.stream().map(e -> HistoryItem.builder()
                .version(e.getVersion())
                .content(e.getContent())
                .operator(e.getOperator())
                .changedAt(e.getChangedAt())
                .changeType(e.getChangeType())
                .diff(e.getDiff() == null ? "" : e.getDiff())
                .build()).toList();
        return ApiResponse.ok(items);
    }

    @GetMapping("/config/history/{version}")
    public ApiResponse<HistoryItem> historyByVersion(@PathVariable("version") long version,
                                                     @RequestParam("application") String app,
                                                     @RequestParam("profile") String profile,
                                                     @RequestParam(value = "key", defaultValue = "") String key) {
        ConfigHistoryEntity e = service.getHistoryVersion(ConfigKeyEntity.of(app, profile, key), version);
        if (e == null) {
            return ApiResponse.error("version not found");
        }
        return ApiResponse.ok(HistoryItem.builder()
                .version(e.getVersion())
                .content(e.getContent())
                .operator(e.getOperator())
                .changedAt(e.getChangedAt())
                .changeType(e.getChangeType())
                .diff(e.getDiff() == null ? "" : e.getDiff())
                .build());
    }

    @PostMapping("/config/rollback")
    public ApiResponse<ConfigView> rollback(@RequestBody RollbackRequest req) {
        ConfigKeyEntity k = ConfigKeyEntity.of(req.getApplication(), req.getProfile(), req.getKey());
        String operator = (req.getOperator() == null || req.getOperator().isBlank()) ? "web" : req.getOperator();
        CurrentRecord next = service.rollback(k, req.getTargetVersion(), operator);
        return ApiResponse.ok(toView(k, next));
    }

    // ------------------- grayscale -------------------

    @PostMapping("/gray/start")
    public ApiResponse<GrayscaleStatusView> startGray(@RequestBody GrayscaleStartRequest req) {
        ConfigKeyEntity k = ConfigKeyEntity.of(req.getApplication(), req.getProfile(), req.getKey());
        String operator = (req.getOperator() == null || req.getOperator().isBlank()) ? "web" : req.getOperator();
        GrayscaleBatchEntity batch = grayManager.start(k, req.getContent(),
                req.getPercent(), req.getObserveWindowSec(), operator, req.getErrorThresholdPct());
        return ApiResponse.ok(toStatusView(batch));
    }

    @PostMapping("/gray/{batchId}/promote")
    public ApiResponse<GrayscaleStatusView> promoteGray(@PathVariable("batchId") String batchId,
                                                         @RequestParam(value = "operator", defaultValue = "web") String operator) {
        GrayscaleBatchEntity batch = grayManager.promote(batchId, operator);
        return ApiResponse.ok(toStatusView(batch));
    }

    @PostMapping("/gray/{batchId}/cancel")
    public ApiResponse<GrayscaleStatusView> cancelGray(@PathVariable("batchId") String batchId,
                                                        @RequestParam(value = "operator", defaultValue = "web") String operator) {
        GrayscaleBatchEntity batch = grayManager.cancel(batchId, operator);
        return ApiResponse.ok(toStatusView(batch));
    }

    @GetMapping("/gray/{batchId}/status")
    public ApiResponse<GrayscaleStatusView> grayStatus(@PathVariable("batchId") String batchId) {
        GrayscaleStatus st = grayManager.getStatus(batchId);
        if (st == null) return ApiResponse.error("batch not found");
        return ApiResponse.ok(toStatusView(st));
    }

    @GetMapping("/gray/batches")
    public ApiResponse<List<GrayscaleBatchView>> grayBatches(@RequestParam(value = "onlyActive", defaultValue = "true") boolean onlyActive) {
        List<GrayscaleBatchEntity> list = grayRepo.listBatches(onlyActive);
        return ApiResponse.ok(list.stream().map(ConfigController::toBatchView).toList());
    }

    @GetMapping("/gray/active")
    public ApiResponse<GrayscaleBatchView> activeGray(@RequestParam("application") String app,
                                                       @RequestParam("profile") String profile,
                                                       @RequestParam(value = "key", defaultValue = "") String key) {
        GrayscaleBatchEntity batch = grayRepo.getActiveBatch(ConfigKeyEntity.of(app, profile, key));
        if (batch == null) return ApiResponse.ok(null);
        return ApiResponse.ok(toBatchView(batch));
    }

    // ------------------- clients -------------------

    @GetMapping("/clients")
    public ApiResponse<List<com.configcenter.server.web.dto.ConfigView.ClientView>> listClients(
            @RequestParam(value = "application", required = false) String app,
            @RequestParam(value = "profile", required = false) String profile,
            @RequestParam(value = "key", required = false) String key) {
        List<ClientStateEntity> clients = grayRepo.listClients(app, profile);
        final String lookupKey = (key == null) ? "" : key;
        return ApiResponse.ok(clients.stream().map(c -> {
            boolean gray = false;
            String batchId = "";
            if (app != null && profile != null) {
                GrayscaleBatchEntity b = grayRepo.getActiveBatch(ConfigKeyEntity.of(app, profile, lookupKey));
                if (b != null && GrayscaleManager.inGraySet(c, b.getPercent())) {
                    gray = true;
                    batchId = b.getBatchId();
                }
            }
            return com.configcenter.server.web.dto.ConfigView.ClientView.builder()
                    .instanceId(c.getInstanceId())
                    .clientIp(c.getClientIp())
                    .application(c.getApplication())
                    .profile(c.getProfile())
                    .version(c.getVersion())
                    .healthy(c.isHealthy())
                    .errorMessage(c.getErrorMessage())
                    .lastSeenAt(c.getLastSeenAt())
                    .grayscale(gray)
                    .batchId(batchId)
                    .build();
        }).toList());
    }

    // ------------------- error handler -------------------

    @ExceptionHandler(StoreException.class)
    @ResponseStatus(HttpStatus.BAD_REQUEST)
    public ApiResponse<Void> handleStore(StoreException e) {
        log.warn("store error: {}", e.getMessage());
        return ApiResponse.error(e.getMessage());
    }

    @ExceptionHandler(Exception.class)
    @ResponseStatus(HttpStatus.INTERNAL_SERVER_ERROR)
    public ApiResponse<Void> handleGeneral(Exception e) {
        log.error("unexpected error", e);
        return ApiResponse.error(e.getMessage());
    }

    private ConfigView toView(ConfigKeyEntity k, CurrentRecord r) {
        return ConfigView.builder()
                .application(k.getApplication())
                .profile(k.getProfile())
                .key(k.getKey())
                .content(r.getContent())
                .version(r.getVersion())
                .updatedBy(r.getUpdatedBy())
                .updatedAt(r.getUpdatedAt())
                .build();
    }

    private static GrayscaleBatchView toBatchView(GrayscaleBatchEntity b) {
        return GrayscaleBatchView.builder()
                .batchId(b.getBatchId())
                .application(b.getKey().getApplication())
                .profile(b.getKey().getProfile())
                .key(b.getKey().getKey())
                .content(b.getContent())
                .targetVersion(b.getTargetVersion())
                .percent(b.getPercent())
                .observeWindowSec(b.getObserveWindowSec())
                .operator(b.getOperator())
                .createdAt(b.getCreatedAt())
                .status(b.getStatus())
                .errorThresholdPct(b.getErrorThresholdPct())
                .resolvedAt(b.getResolvedAt())
                .resolution(b.getResolution())
                .diff(b.getDiff())
                .build();
    }

    private GrayscaleStatusView toStatusView(GrayscaleBatchEntity batch) {
        int[] s = grayManager.computeStats(batch);
        long elapsed = (System.currentTimeMillis() - batch.getCreatedAt()) / 1000L;
        long remaining = Math.max(0L, batch.getObserveWindowSec() - elapsed);
        return GrayscaleStatusView.builder()
                .batch(toBatchView(batch))
                .totalClients(s[0])
                .grayClients(s[2])
                .healthyClients(s[3])
                .errorClients(s[1])
                .upgradedClients(s[4])
                .errorRatePct(s[2] == 0 ? 0.0 : (double) s[1] / s[2] * 100.0)
                .remainingSec(batch.isActive() ? remaining : 0L)
                .build();
    }

    private GrayscaleStatusView toStatusView(GrayscaleStatus st) {
        return GrayscaleStatusView.builder()
                .batch(toBatchView(st.batch()))
                .totalClients(st.totalClients())
                .grayClients(st.grayClients())
                .healthyClients(st.healthyClients())
                .errorClients(st.errorClients())
                .upgradedClients(st.upgradedClients())
                .errorRatePct(st.errorRatePct())
                .remainingSec(st.remainingSec())
                .build();
    }
}
