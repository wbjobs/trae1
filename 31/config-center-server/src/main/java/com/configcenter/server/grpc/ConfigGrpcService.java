package com.configcenter.server.grpc;

import com.configcenter.proto.*;
import com.configcenter.server.gray.GrayscaleManager;
import com.configcenter.server.gray.GrayscaleManager.GrayscaleStatus;
import com.configcenter.server.model.ClientStateEntity;
import com.configcenter.server.model.ConfigKeyEntity;
import com.configcenter.server.model.GrayscaleBatchEntity;
import com.configcenter.server.service.ConfigService;
import com.configcenter.server.store.ConfigRepository.CurrentRecord;
import com.configcenter.server.store.GrayscaleRepository;
import com.configcenter.server.watch.WatchManager;
import io.grpc.Status;
import io.grpc.stub.StreamObserver;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;

import java.util.List;

@Slf4j
@RequiredArgsConstructor
public class ConfigGrpcService extends ConfigServiceGrpc.ConfigServiceImplBase {

    private final ConfigService service;
    private final WatchManager watchManager;
    private final GrayscaleRepository grayRepo;
    private final GrayscaleManager grayManager;

    @Override
    public void getConfig(GetConfigRequest request, StreamObserver<GetConfigResponse> responseObserver) {
        try {
            ConfigKeyEntity key = toKey(request.getConfigKey());
            CurrentRecord record = service.getCurrent(key);
            if (record == null) {
                responseObserver.onError(Status.NOT_FOUND.withDescription("config not found").asRuntimeException());
                return;
            }
            responseObserver.onNext(GetConfigResponse.newBuilder().setEntry(toProto(key, record)).build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void publishConfig(PublishConfigRequest request, StreamObserver<PublishConfigResponse> responseObserver) {
        try {
            ConfigKeyEntity key = toKey(request.getConfigKey());
            GrayscaleBatchEntity active = grayRepo.getActiveBatch(key);
            if (active != null) {
                responseObserver.onError(Status.FAILED_PRECONDITION
                        .withDescription("active grayscale batch exists for this config: " + active.getBatchId())
                        .asRuntimeException());
                return;
            }
            CurrentRecord next = service.publish(key, request.getContent(),
                    request.getOperator().isEmpty() ? "grpc" : request.getOperator());
            responseObserver.onNext(PublishConfigResponse.newBuilder().setEntry(toProto(key, next)).build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            log.error("publish error", e);
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void watchConfig(WatchConfigRequest request,
                            StreamObserver<WatchConfigResponse> responseObserver) {
        ConfigKeyEntity key = toKey(request.getConfigKey());
        String instanceId = request.getInstanceId();
        watchManager.register(key, instanceId, responseObserver);

        // Send initial push based on grayscale state.
        try {
            GrayscaleBatchEntity active = grayRepo.getActiveBatch(key);
            ClientStateEntity client = instanceId == null ? null : grayRepo.getClient(instanceId);
            boolean inGray = active != null && client != null
                    && GrayscaleManager.inGraySet(client, active.getPercent());

            CurrentRecord record;
            boolean grayscale;
            String batchId;
            if (inGray) {
                record = CurrentRecord.builder()
                        .application(key.getApplication())
                        .profile(key.getProfile())
                        .key(key.getKey())
                        .content(active.getContent())
                        .version(active.getTargetVersion())
                        .updatedBy(active.getOperator())
                        .updatedAt(System.currentTimeMillis())
                        .build();
                grayscale = true;
                batchId = active.getBatchId();
            } else {
                record = service.getCurrent(key);
                grayscale = false;
                batchId = "";
            }
            if (record != null && (request.getSinceVersion() == 0 || record.getVersion() > request.getSinceVersion())) {
                responseObserver.onNext(WatchConfigResponse.newBuilder()
                        .setEntry(toProto(key, record))
                        .setEventType("MODIFIED")
                        .setGrayscale(grayscale)
                        .setBatchId(batchId)
                        .build());
            }
        } catch (Exception e) {
            log.warn("watch initial push failed for {}/{}: {}",
                    key.getApplication(), key.getKey(), e.getMessage());
        }
    }

    @Override
    public void getHistory(HistoryRequest request, StreamObserver<HistoryResponse> responseObserver) {
        try {
            ConfigKeyEntity key = toKey(request.getConfigKey());
            List<com.configcenter.server.model.ConfigHistoryEntity> entries =
                    service.getHistory(key, request.getLimit());
            HistoryResponse.Builder b = HistoryResponse.newBuilder();
            for (com.configcenter.server.model.ConfigHistoryEntity e : entries) {
                b.addEntries(HistoryEntry.newBuilder()
                        .setVersion(e.getVersion())
                        .setContent(e.getContent())
                        .setOperator(e.getOperator())
                        .setChangedAt(e.getChangedAt())
                        .setChangeType(e.getChangeType())
                        .setDiff(e.getDiff() == null ? "" : e.getDiff())
                        .build());
            }
            responseObserver.onNext(b.build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void rollback(RollbackRequest request, StreamObserver<RollbackResponse> responseObserver) {
        try {
            ConfigKeyEntity key = toKey(request.getConfigKey());
            CurrentRecord next = service.rollback(key, request.getTargetVersion(),
                    request.getOperator().isEmpty() ? "grpc" : request.getOperator());
            responseObserver.onNext(RollbackResponse.newBuilder().setEntry(toProto(key, next)).build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void listApps(ListAppsRequest request, StreamObserver<ListAppsResponse> responseObserver) {
        try {
            responseObserver.onNext(ListAppsResponse.newBuilder().addAllApplications(service.listApplications()).build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void listProfiles(ListProfilesRequest request, StreamObserver<ListProfilesResponse> responseObserver) {
        try {
            responseObserver.onNext(ListProfilesResponse.newBuilder().addAllProfiles(service.listProfiles(request.getApplication())).build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void listKeys(ListKeysRequest request, StreamObserver<ListKeysResponse> responseObserver) {
        try {
            List<com.configcenter.server.model.ConfigKeyEntity> keys =
                    service.listKeys(request.getApplication(), request.getProfile());
            ListKeysResponse.Builder b = ListKeysResponse.newBuilder();
            for (com.configcenter.server.model.ConfigKeyEntity k : keys) b.addKeys(k.getKey());
            responseObserver.onNext(b.build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    // ------------------------------------------------------------------
    //  grayscale RPCs
    // ------------------------------------------------------------------

    @Override
    public void startGrayscale(StartGrayscaleRequest request,
                               StreamObserver<StartGrayscaleResponse> responseObserver) {
        try {
            ConfigKeyEntity key = toKey(request.getConfigKey());
            GrayscaleBatchEntity batch = grayManager.start(key, request.getContent(),
                    request.getPercent() == 0 ? null : request.getPercent(),
                    request.getObserveWindowSec() == 0 ? null : request.getObserveWindowSec(),
                    request.getOperator().isEmpty() ? "grpc" : request.getOperator(),
                    request.getErrorThresholdPct() == 0 ? null : request.getErrorThresholdPct());
            ConfigEntry grayEntry = ConfigEntry.newBuilder()
                    .setConfigKey(request.getConfigKey())
                    .setContent(batch.getContent())
                    .setVersion(batch.getTargetVersion())
                    .setUpdatedAt(batch.getCreatedAt())
                    .setUpdatedBy(batch.getOperator())
                    .build();
            responseObserver.onNext(StartGrayscaleResponse.newBuilder()
                    .setBatch(toBatchProto(batch)).setGrayEntry(grayEntry).build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void cancelGrayscale(CancelGrayscaleRequest request,
                                StreamObserver<CancelGrayscaleResponse> responseObserver) {
        try {
            GrayscaleBatchEntity batch = grayManager.cancel(request.getBatchId(),
                    request.getOperator().isEmpty() ? "grpc" : request.getOperator());
            responseObserver.onNext(CancelGrayscaleResponse.newBuilder().setBatch(toBatchProto(batch)).build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void promoteGrayscale(PromoteGrayscaleRequest request,
                                 StreamObserver<PromoteGrayscaleResponse> responseObserver) {
        try {
            GrayscaleBatchEntity batch = grayManager.promote(request.getBatchId(),
                    request.getOperator().isEmpty() ? "grpc" : request.getOperator());
            CurrentRecord cur = service.getCurrent(batch.getKey());
            ConfigEntry promoted = cur == null ? ConfigEntry.newBuilder().build() : toProto(batch.getKey(), cur);
            responseObserver.onNext(PromoteGrayscaleResponse.newBuilder()
                    .setBatch(toBatchProto(batch)).setPromotedEntry(promoted).build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void getGrayscaleStatus(GetGrayscaleStatusRequest request,
                                   StreamObserver<GetGrayscaleStatusResponse> responseObserver) {
        try {
            GrayscaleStatus st = grayManager.getStatus(request.getBatchId());
            if (st == null) {
                responseObserver.onError(Status.NOT_FOUND.withDescription("batch not found").asRuntimeException());
                return;
            }
            responseObserver.onNext(GetGrayscaleStatusResponse.newBuilder()
                    .setBatch(toBatchProto(st.batch()))
                    .setTotalClients(st.totalClients())
                    .setGrayClients(st.grayClients())
                    .setHealthyClients(st.healthyClients())
                    .setErrorClients(st.errorClients())
                    .setUpgradedClients(st.upgradedClients())
                    .setErrorRatePct(st.errorRatePct())
                    .setRemainingSec(st.remainingSec())
                    .build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void listGrayscaleBatches(ListGrayscaleBatchesRequest request,
                                     StreamObserver<ListGrayscaleBatchesResponse> responseObserver) {
        try {
            List<GrayscaleBatchEntity> list = grayRepo.listBatches(request.getOnlyActive());
            ListGrayscaleBatchesResponse.Builder b = ListGrayscaleBatchesResponse.newBuilder();
            for (GrayscaleBatchEntity be : list) b.addBatches(toBatchProto(be));
            responseObserver.onNext(b.build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void reportClientState(ClientHeartbeat request,
                                  StreamObserver<ClientStateAck> responseObserver) {
        try {
            ClientStateEntity entity = ClientStateEntity.builder()
                    .instanceId(request.getInstanceId())
                    .clientIp(request.getClientIp())
                    .application(request.getApplication())
                    .profile(request.getProfile())
                    .version(request.getVersion())
                    .healthy(request.getHealthy())
                    .errorMessage(request.getErrorMessage())
                    .lastSeenAt(request.getReportedAt() == 0 ? System.currentTimeMillis() : request.getReportedAt())
                    .build();
            grayRepo.saveClient(entity);
            responseObserver.onNext(ClientStateAck.newBuilder().setOk(true).setMessage("ok").build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    @Override
    public void listClients(ListClientsRequest request,
                            StreamObserver<ListClientsResponse> responseObserver) {
        try {
            String app = request.getApplication().isEmpty() ? null : request.getApplication();
            String profile = request.getProfile().isEmpty() ? null : request.getProfile();
            List<ClientStateEntity> clients = grayRepo.listClients(app, profile);
            ListClientsResponse.Builder b = ListClientsResponse.newBuilder();
            for (ClientStateEntity c : clients) {
                b.addClients(ClientView.newBuilder()
                        .setInstanceId(c.getInstanceId())
                        .setClientIp(c.getClientIp())
                        .setApplication(c.getApplication())
                        .setProfile(c.getProfile())
                        .setVersion(c.getVersion())
                        .setHealthy(c.isHealthy())
                        .setErrorMessage(c.getErrorMessage() == null ? "" : c.getErrorMessage())
                        .setLastSeenAt(c.getLastSeenAt())
                        .build());
            }
            responseObserver.onNext(b.build());
            responseObserver.onCompleted();
        } catch (Exception e) {
            responseObserver.onError(Status.INTERNAL.withDescription(e.getMessage()).asRuntimeException());
        }
    }

    // ------------------------------------------------------------------
    //  helpers
    // ------------------------------------------------------------------

    private ConfigKeyEntity toKey(ConfigKey k) {
        return ConfigKeyEntity.of(k.getApplication(), k.getProfile(), k.getKey());
    }

    private ConfigEntry toProto(ConfigKeyEntity key, CurrentRecord r) {
        return ConfigEntry.newBuilder()
                .setConfigKey(ConfigKey.newBuilder()
                        .setApplication(key.getApplication())
                        .setProfile(key.getProfile())
                        .setKey(key.getKey() == null ? "" : key.getKey())
                        .build())
                .setContent(r.getContent())
                .setVersion(r.getVersion())
                .setUpdatedAt(r.getUpdatedAt())
                .setUpdatedBy(r.getUpdatedBy() == null ? "" : r.getUpdatedBy())
                .build();
    }

    private GrayscaleBatch toBatchProto(GrayscaleBatchEntity b) {
        return GrayscaleBatch.newBuilder()
                .setBatchId(b.getBatchId())
                .setConfigKey(ConfigKey.newBuilder()
                        .setApplication(b.getKey().getApplication())
                        .setProfile(b.getKey().getProfile())
                        .setKey(b.getKey().getKey())
                        .build())
                .setContent(b.getContent())
                .setTargetVersion(b.getTargetVersion())
                .setPercent(b.getPercent())
                .setObserveWindowSec(b.getObserveWindowSec())
                .setOperator(b.getOperator())
                .setCreatedAt(b.getCreatedAt())
                .setStatus(b.getStatus())
                .setErrorThresholdPct(b.getErrorThresholdPct())
                .setResolvedAt(b.getResolvedAt())
                .setResolution(b.getResolution() == null ? "" : b.getResolution())
                .setDiff(b.getDiff() == null ? "" : b.getDiff())
                .build();
    }
}
