package com.configcenter.server.store;

import com.configcenter.server.config.AppConfig;
import com.configcenter.server.model.ClientStateEntity;
import com.configcenter.server.model.ConfigKeyEntity;
import com.configcenter.server.model.GrayscaleBatchEntity;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.etcd.jetcd.ByteSequence;
import io.etcd.jetcd.Client;
import io.etcd.jetcd.KeyValue;
import io.etcd.jetcd.kv.GetResponse;
import io.etcd.jetcd.options.GetOption;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Repository;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutionException;

@Slf4j
@Repository
public class GrayscaleRepository {

    private final Client etcd;
    private final String prefix;
    private final ObjectMapper mapper;

    public GrayscaleRepository(Client etcd, AppConfig appConfig, ObjectMapper mapper) {
        this.etcd = etcd;
        this.prefix = appConfig.getEtcd().getPrefix();
        this.mapper = mapper;
    }

    // ------------------------------------------------------------------
    //  batch CRUD
    // ------------------------------------------------------------------

    public void saveBatch(GrayscaleBatchEntity batch) {
        String k = GrayPaths.batchKey(prefix, batch.getBatchId());
        try {
            etcd.getKVClient().put(bytes(k), bytes(mapper.writeValueAsString(batch))).get();
        } catch (Exception e) {
            throw new StoreException("saveBatch failed", e);
        }
    }

    public GrayscaleBatchEntity getBatch(String batchId) {
        String k = GrayPaths.batchKey(prefix, batchId);
        try {
            GetResponse resp = etcd.getKVClient().get(bytes(k)).get();
            if (resp.getKvs().isEmpty()) return null;
            return mapper.readValue(resp.getKvs().get(0).getValue().getBytes(), GrayscaleBatchEntity.class);
        } catch (Exception e) {
            throw new StoreException("getBatch failed", e);
        }
    }

    public List<GrayscaleBatchEntity> listBatches(boolean onlyActive) {
        String p = GrayPaths.batchesPrefix(prefix);
        try {
            GetResponse resp = etcd.getKVClient().get(bytes(p),
                    GetOption.newBuilder().withPrefix(bytes(p))
                            .withSortField(GetOption.SortTarget.CREATE)
                            .withSortOrder(GetOption.SortOrder.DESCEND)
                            .build()).get();
            List<GrayscaleBatchEntity> list = new ArrayList<>();
            for (KeyValue kv : resp.getKvs()) {
                GrayscaleBatchEntity b = mapper.readValue(kv.getValue().getBytes(), GrayscaleBatchEntity.class);
                if (!onlyActive || b.isActive()) list.add(b);
            }
            return list;
        } catch (Exception e) {
            throw new StoreException("listBatches failed", e);
        }
    }

    // ------------------------------------------------------------------
    //  active batch index: one active batch per config key
    // ------------------------------------------------------------------

    public void setActiveBatch(ConfigKeyEntity key, String batchId) {
        String k = GrayPaths.activeKey(prefix, key.getApplication(), key.getProfile(), key.getKey());
        try {
            etcd.getKVClient().put(bytes(k), bytes(batchId)).get();
        } catch (Exception e) {
            throw new StoreException("setActiveBatch failed", e);
        }
    }

    public String getActiveBatchId(ConfigKeyEntity key) {
        String k = GrayPaths.activeKey(prefix, key.getApplication(), key.getProfile(), key.getKey());
        try {
            GetResponse resp = etcd.getKVClient().get(bytes(k)).get();
            if (resp.getKvs().isEmpty()) return null;
            return resp.getKvs().get(0).getValue().toString(StandardCharsets.UTF_8);
        } catch (Exception e) {
            throw new StoreException("getActiveBatchId failed", e);
        }
    }

    public GrayscaleBatchEntity getActiveBatch(ConfigKeyEntity key) {
        String batchId = getActiveBatchId(key);
        if (batchId == null) return null;
        return getBatch(batchId);
    }

    public void clearActiveBatch(ConfigKeyEntity key) {
        String k = GrayPaths.activeKey(prefix, key.getApplication(), key.getProfile(), key.getKey());
        try {
            etcd.getKVClient().delete(bytes(k)).get();
        } catch (Exception e) {
            throw new StoreException("clearActiveBatch failed", e);
        }
    }

    // ------------------------------------------------------------------
    //  client state
    // ------------------------------------------------------------------

    public void saveClient(ClientStateEntity client) {
        String k = GrayPaths.clientKey(prefix, client.getInstanceId());
        try {
            etcd.getKVClient().put(bytes(k), bytes(mapper.writeValueAsString(client))).get();
        } catch (Exception e) {
            throw new StoreException("saveClient failed", e);
        }
    }

    public ClientStateEntity getClient(String instanceId) {
        String k = GrayPaths.clientKey(prefix, instanceId);
        try {
            GetResponse resp = etcd.getKVClient().get(bytes(k)).get();
            if (resp.getKvs().isEmpty()) return null;
            return mapper.readValue(resp.getKvs().get(0).getValue().getBytes(), ClientStateEntity.class);
        } catch (Exception e) {
            throw new StoreException("getClient failed", e);
        }
    }

    public List<ClientStateEntity> listClients(String application, String profile) {
        String p = GrayPaths.clientsPrefix(prefix);
        try {
            GetResponse resp = etcd.getKVClient().get(bytes(p),
                    GetOption.newBuilder().withPrefix(bytes(p)).build()).get();
            List<ClientStateEntity> list = new ArrayList<>();
            for (KeyValue kv : resp.getKvs()) {
                ClientStateEntity c = mapper.readValue(kv.getValue().getBytes(), ClientStateEntity.class);
                if (application != null && !application.equals(c.getApplication())) continue;
                if (profile != null && !profile.equals(c.getProfile())) continue;
                list.add(c);
            }
            return list;
        } catch (Exception e) {
            throw new StoreException("listClients failed", e);
        }
    }

    public List<ClientStateEntity> listAllClients() {
        return listClients(null, null);
    }

    private static ByteSequence bytes(String s) {
        return ByteSequence.from(s, StandardCharsets.UTF_8);
    }
}
