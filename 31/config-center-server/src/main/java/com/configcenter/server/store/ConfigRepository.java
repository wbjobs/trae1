package com.configcenter.server.store;

import com.configcenter.server.model.ConfigHistoryEntity;
import com.configcenter.server.model.ConfigKeyEntity;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.etcd.jetcd.ByteSequence;
import io.etcd.jetcd.Client;
import io.etcd.jetcd.KeyValue;
import io.etcd.jetcd.kv.GetResponse;
import io.etcd.jetcd.kv.PutResponse;
import io.etcd.jetcd.kv.TxnResponse;
import io.etcd.jetcd.op.Op;
import io.etcd.jetcd.options.DeleteOption;
import io.etcd.jetcd.options.GetOption;
import io.etcd.jetcd.options.PutOption;
import jakarta.annotation.PreDestroy;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Repository;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ExecutionException;

@Slf4j
@Repository
public class ConfigRepository {

    private final Client etcd;
    private final String prefix;
    private final ObjectMapper mapper;

    public ConfigRepository(Client etcd, com.configcenter.server.config.AppConfig appConfig, ObjectMapper mapper) {
        this.etcd = etcd;
        this.prefix = appConfig.getEtcd().getPrefix();
        this.mapper = mapper;
    }

    @PreDestroy
    public void close() {
        try {
            etcd.close();
        } catch (Exception e) {
            log.warn("error closing etcd client", e);
        }
    }

    // ------------------------------------------------------------------
    // index helpers
    // ------------------------------------------------------------------

    public List<String> listApplications() {
        return readIndex(KeyPaths.appsIndex(prefix));
    }

    public void registerApplication(String application) {
        addToIndex(KeyPaths.appsIndex(prefix), application);
    }

    // ------------------------------------------------------------------
    // current value
    // ------------------------------------------------------------------

    public CurrentRecord getCurrent(ConfigKeyEntity key) {
        String k = KeyPaths.currentKey(prefix, key);
        try {
            GetResponse resp = etcd.getKVClient().get(bytes(k)).get();
            if (resp.getKvs().isEmpty()) return null;
            KeyValue kv = resp.getKvs().get(0);
            String value = kv.getValue().toString(StandardCharsets.UTF_8);
            CurrentRecord r = mapper.readValue(value, CurrentRecord.class);
            r.setModRevision(kv.getModRevision());
            return r;
        } catch (Exception e) {
            throw new StoreException("getCurrent failed for " + k, e);
        }
    }

    /**
     * Atomically write new current config and append a history record.
     * Returns the new version number.
     */
    public CurrentRecord write(ConfigKeyEntity key, String content, String operator, String changeType,
                               CurrentRecord previous) {
        long nextVersion = previous == null ? 1L : previous.getVersion() + 1L;
        CurrentRecord next = CurrentRecord.builder()
                .application(key.getApplication())
                .profile(key.getProfile())
                .key(key.getKey())
                .content(content)
                .version(nextVersion)
                .updatedBy(operator)
                .updatedAt(System.currentTimeMillis())
                .build();

        ConfigHistoryEntity history = ConfigHistoryEntity.builder()
                .key(key)
                .version(nextVersion)
                .content(content)
                .operator(operator)
                .changedAt(next.getUpdatedAt())
                .changeType(changeType)
                .diff(previous == null ? "initial creation" : diffSummary(previous.getContent(), content))
                .build();

        String currentKey = KeyPaths.currentKey(prefix, key);
        String historyKey = KeyPaths.historyKey(prefix, key, nextVersion);

        try {
            String currentJson = mapper.writeValueAsString(next);
            String historyJson = mapper.writeValueAsString(history);

            // Use a transaction with CAS based on mod revision for safety.
            Op.Put putCurrent = Op.put(bytes(currentKey), bytes(currentJson), PutOption.DEFAULT);
            Op.Put putHistory = Op.put(bytes(historyKey), bytes(historyJson), PutOption.DEFAULT);

            Op.Target target = Op.Target.key(bytes(currentKey));
            if (previous == null) {
                // expect key not yet present
                TxnResponse txn = etcd.getKVClient().txnIf()
                        .cmp(Op.Cmp.modRevision(target).equal(0L))
                        .Then(putCurrent, putHistory)
                        .commit()
                        .get();
                if (!txn.isSucceeded()) {
                    throw new StoreException("concurrent write for " + currentKey);
                }
            } else {
                long prevMod = previous.getModRevision();
                TxnResponse txn = etcd.getKVClient().txnIf()
                        .cmp(Op.Cmp.modRevision(target).equal(prevMod))
                        .Then(putCurrent, putHistory)
                        .commit()
                        .get();
                if (!txn.isSucceeded()) {
                    throw new StoreException("concurrent write for " + currentKey);
                }
            }

            registerApplication(key.getApplication());

            log.info("config written app={} profile={} key={} version={}",
                    key.getApplication(), key.getProfile(), key.getKey(), nextVersion);
            return next;
        } catch (StoreException e) {
            throw e;
        } catch (Exception e) {
            throw new StoreException("write failed for " + currentKey, e);
        }
    }

    public void delete(ConfigKeyEntity key, CurrentRecord previous, String operator) {
        long nextVersion = (previous == null ? 0L : previous.getVersion() + 1L);
        String currentKey = KeyPaths.currentKey(prefix, key);
        String historyKey = KeyPaths.historyKey(prefix, key, nextVersion);

        ConfigHistoryEntity history = ConfigHistoryEntity.builder()
                .key(key)
                .version(nextVersion)
                .content(previous == null ? "" : previous.getContent())
                .operator(operator)
                .changedAt(System.currentTimeMillis())
                .changeType(ConfigHistoryEntity.typeDelete())
                .diff("deleted")
                .build();

        try {
            Op.Delete delCurrent = Op.delete(bytes(currentKey), DeleteOption.DEFAULT);
            Op.Put putHistory = Op.put(bytes(historyKey), bytes(mapper.writeValueAsString(history)), PutOption.DEFAULT);
            etcd.getKVClient().txnIf().Then(delCurrent, putHistory).commit().get();
        } catch (Exception e) {
            throw new StoreException("delete failed", e);
        }
    }

    // ------------------------------------------------------------------
    // history
    // ------------------------------------------------------------------

    public List<ConfigHistoryEntity> getHistory(ConfigKeyEntity key, int limit) {
        String prefixKey = KeyPaths.historyPrefix(prefix, key);
        try {
            GetResponse resp = etcd.getKVClient().get(bytes(prefixKey),
                    GetOption.newBuilder().withPrefix(bytes(prefixKey))
                            .withSortField(GetOption.SortTarget.KEY)
                            .withSortOrder(GetOption.SortOrder.DESCEND)
                            .withLimit(limit <= 0 ? Integer.MAX_VALUE : limit)
                            .build()).get();
            List<ConfigHistoryEntity> result = new ArrayList<>();
            for (KeyValue kv : resp.getKvs()) {
                result.add(mapper.readValue(kv.getValue().getBytes(), ConfigHistoryEntity.class));
            }
            return result;
        } catch (Exception e) {
            throw new StoreException("getHistory failed", e);
        }
    }

    public ConfigHistoryEntity getHistoryByVersion(ConfigKeyEntity key, long version) {
        String k = KeyPaths.historyKey(prefix, key, version);
        try {
            GetResponse resp = etcd.getKVClient().get(bytes(k)).get();
            if (resp.getKvs().isEmpty()) return null;
            return mapper.readValue(resp.getKvs().get(0).getValue().getBytes(), ConfigHistoryEntity.class);
        } catch (Exception e) {
            throw new StoreException("getHistoryByVersion failed", e);
        }
    }

    // ------------------------------------------------------------------
    // list keys
    // ------------------------------------------------------------------

    public List<ConfigKeyEntity> listKeysUnderProfile(String application, String profile) {
        String prefixKey = KeyPaths.currentProfilePrefix(prefix, application, profile);
        try {
            GetResponse resp = etcd.getKVClient().get(bytes(prefixKey),
                    GetOption.newBuilder().withPrefix(bytes(prefixKey)).withKeysOnly(true).build()).get();
            List<ConfigKeyEntity> list = new ArrayList<>();
            for (KeyValue kv : resp.getKvs()) {
                String k = kv.getKey().toString(StandardCharsets.UTF_8);
                String suffix = k.substring(prefixKey.length());
                list.add(ConfigKeyEntity.of(application, profile, suffix));
            }
            return list;
        } catch (Exception e) {
            throw new StoreException("listKeysUnderProfile failed", e);
        }
    }

    // ------------------------------------------------------------------
    // index management
    // ------------------------------------------------------------------

    private List<String> readIndex(String indexKey) {
        try {
            GetResponse resp = etcd.getKVClient().get(bytes(indexKey)).get();
            if (resp.getKvs().isEmpty()) return new ArrayList<>();
            String val = resp.getKvs().get(0).getValue().toString(StandardCharsets.UTF_8);
            return new ArrayList<>(parseIndex(val));
        } catch (Exception e) {
            return new ArrayList<>();
        }
    }

    private void addToIndex(String indexKey, String value) {
        if (value == null || value.isEmpty()) return;
        try {
            GetResponse resp = etcd.getKVClient().get(bytes(indexKey)).get();
            String current = "";
            long modRevision = 0L;
            if (!resp.getKvs().isEmpty()) {
                current = resp.getKvs().get(0).getValue().toString(StandardCharsets.UTF_8);
                modRevision = resp.getKvs().get(0).getModRevision();
            }
            java.util.Set<String> items = new java.util.LinkedHashSet<>(parseIndex(current));
            if (items.add(value)) {
                String newVal = String.join(",", items);
                if (modRevision == 0L) {
                    etcd.getKVClient().put(bytes(indexKey), bytes(newVal)).get();
                } else {
                    TxnResponse txn = etcd.getKVClient().txnIf()
                            .cmp(Op.Cmp.modRevision(Op.Target.key(bytes(indexKey))).equal(modRevision))
                            .Then(Op.put(bytes(indexKey), bytes(newVal), PutOption.DEFAULT))
                            .commit().get();
                    if (!txn.isSucceeded()) {
                        // retry once
                        resp = etcd.getKVClient().get(bytes(indexKey)).get();
                        current = resp.getKvs().isEmpty() ? "" : resp.getKvs().get(0).getValue().toString(StandardCharsets.UTF_8);
                        items = new java.util.LinkedHashSet<>(parseIndex(current));
                        if (items.add(value)) {
                            etcd.getKVClient().put(bytes(indexKey), bytes(String.join(",", items))).get();
                        }
                    }
                }
            }
        } catch (Exception e) {
            log.warn("addToIndex failed key={} value={}: {}", indexKey, value, e.getMessage());
        }
    }

    private java.util.List<String> parseIndex(String raw) {
        if (raw == null || raw.isEmpty()) return Collections.emptyList();
        return java.util.Arrays.stream(raw.split(",")).map(String::trim)
                .filter(s -> !s.isEmpty()).toList();
    }

    // ------------------------------------------------------------------
    // utilities
    // ------------------------------------------------------------------

    private static ByteSequence bytes(String s) {
        return ByteSequence.from(s, StandardCharsets.UTF_8);
    }

    private static String diffSummary(String a, String b) {
        if (a == null) a = "";
        if (b == null) b = "";
        int la = a.split("\n").length;
        int lb = b.split("\n").length;
        return String.format("lines %d -> %d; len %d -> %d", la, lb, a.length(), b.length());
    }

    static long bytesToLong(byte[] b) {
        if (b == null || b.length == 0) return 0L;
        long v = 0L;
        for (byte bb : b) v = (v << 8) | (bb & 0xffL);
        return v;
    }

    @lombok.Data
    @lombok.Builder
    @lombok.NoArgsConstructor
    @lombok.AllArgsConstructor
    public static class CurrentRecord {
        private String application;
        private String profile;
        private String key;
        private String content;
        private long version;
        private String updatedBy;
        private long updatedAt;
        private long modRevision;
    }

    public static class StoreException extends RuntimeException {
        public StoreException(String msg) { super(msg); }
        public StoreException(String msg, Throwable t) { super(msg, t); }
    }
}
