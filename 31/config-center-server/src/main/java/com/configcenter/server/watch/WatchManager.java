package com.configcenter.server.watch;

import com.configcenter.server.model.ConfigKeyEntity;
import com.configcenter.proto.WatchConfigResponse;
import com.configcenter.server.store.ConfigRepository.CurrentRecord;
import io.grpc.stub.StreamObserver;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Component;

import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;

@Slf4j
@Component
public class WatchManager {

    private final ConcurrentHashMap<WatchKey, List<WatcherRegistration>>
            observers = new ConcurrentHashMap<>();

    public void register(ConfigKeyEntity key, String instanceId,
                         StreamObserver<WatchConfigResponse> observer) {
        observers.computeIfAbsent(WatchKey.from(key), k -> new CopyOnWriteArrayList<>())
                .add(new WatcherRegistration(instanceId, observer));
        log.debug("registered watcher for {}/{}/{} [{}]",
                key.getApplication(), key.getProfile(), key.getKey(), instanceId);
    }

    public void unregister(ConfigKeyEntity key, StreamObserver<WatchConfigResponse> observer) {
        List<WatcherRegistration> list = observers.get(WatchKey.from(key));
        if (list != null) list.removeIf(r -> r.observer == observer);
    }

    /** Push to ALL watchers of this key (full broadcast). */
    public void broadcast(ConfigKeyEntity key, CurrentRecord record, String eventType) {
        broadcastInternal(key, record, eventType, false, null, false, null);
    }

    /** Push to gray watchers only (targetInstanceIds). */
    public void broadcastToGray(ConfigKeyEntity key, CurrentRecord record, String eventType,
                                 String batchId, java.util.Set<String> targetInstanceIds) {
        broadcastInternal(key, record, eventType, true, batchId, true, targetInstanceIds);
    }

    /** Push to non-gray watchers only (e.g. cancel rollback). */
    public void broadcastToNonGray(ConfigKeyEntity key, CurrentRecord record, String eventType,
                                    java.util.Set<String> grayInstanceIds) {
        broadcastInternal(key, record, eventType, false, null, false, grayInstanceIds);
    }

    private void broadcastInternal(ConfigKeyEntity key, CurrentRecord record, String eventType,
                                    boolean grayscale, String batchId,
                                    boolean onlyTarget, java.util.Set<String> filterInstanceIds) {
        List<WatcherRegistration> list = observers.get(WatchKey.from(key));
        if (list == null || list.isEmpty()) return;

        WatchConfigResponse response = WatchConfigResponse.newBuilder()
                .setEntry(com.configcenter.proto.ConfigEntry.newBuilder()
                        .setConfigKey(com.configcenter.proto.ConfigKey.newBuilder()
                                .setApplication(key.getApplication())
                                .setProfile(key.getProfile())
                                .setKey(key.getKey() == null ? "" : key.getKey())
                                .build())
                        .setContent(record.getContent())
                        .setVersion(record.getVersion())
                        .setUpdatedAt(record.getUpdatedAt())
                        .setUpdatedBy(record.getUpdatedBy() == null ? "" : record.getUpdatedBy())
                        .build())
                .setEventType(eventType)
                .setGrayscale(grayscale)
                .setBatchId(batchId == null ? "" : batchId)
                .build();

        List<WatcherRegistration> snapshot = List.copyOf(list);
        for (WatcherRegistration r : snapshot) {
            if (filterInstanceIds != null) {
                boolean inFilter = r.instanceId != null && filterInstanceIds.contains(r.instanceId);
                if (onlyTarget && !inFilter) continue;
                if (!onlyTarget && inFilter) continue;
            }
            try {
                r.observer.onNext(response);
            } catch (Exception e) {
                log.warn("push failed to watcher [{}] for {}/{}/{}: {}",
                        r.instanceId, key.getApplication(), key.getProfile(), key.getKey(), e.getMessage());
                list.remove(r);
                try { r.observer.onCompleted(); } catch (Exception ignored) {}
            }
        }
    }

    private record WatchKey(String application, String profile, String key) {
        static WatchKey from(ConfigKeyEntity k) {
            return new WatchKey(k.getApplication(), k.getProfile(), k.getKey() == null ? "" : k.getKey());
        }
    }

    private record WatcherRegistration(String instanceId, StreamObserver<WatchConfigResponse> observer) {}
}
