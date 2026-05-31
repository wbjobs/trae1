package com.profile.store.service;

import com.profile.store.sharding.ShardedQueryService;
import com.profile.store.sharding.ShardedTagBitmapRepository;
import com.profile.store.sharding.ShardHealthChecker;
import com.profile.store.sharding.ShardRebalanceService;
import com.profile.store.sharding.ShardProperties;
import org.springframework.stereotype.Service;

import java.io.IOException;
import java.util.*;
import java.util.concurrent.ExecutionException;

/**
 * 标签画像服务：封装分片存储和查询操作。
 * <p>
 * 所有对外操作通过此服务，路由到对应的分片进行读写。
 */
@Service
public class TagService {

    private final ShardedTagBitmapRepository repository;
    private final ShardedQueryService queryService;
    private final ShardRebalanceService rebalanceService;
    private final ShardHealthChecker healthChecker;
    private final ShardProperties properties;

    public TagService(ShardedTagBitmapRepository repository,
                      ShardedQueryService queryService,
                      ShardRebalanceService rebalanceService,
                      ShardHealthChecker healthChecker,
                      ShardProperties properties) {
        this.repository = repository;
        this.queryService = queryService;
        this.rebalanceService = rebalanceService;
        this.healthChecker = healthChecker;
        this.properties = properties;
    }

    public void addUser(String tagName, int userId) {
        repository.save(tagName, userId);
    }

    public void addUsers(String tagName, Collection<Integer> userIds) {
        repository.saveUsers(tagName, userIds);
    }

    public void removeUser(String tagName, int userId) {
        repository.remove(tagName, userId);
    }

    public void removeUsers(String tagName, Collection<Integer> userIds) {
        repository.removeUsers(tagName, userIds);
    }

    public long getTagCount(String tagName) {
        return repository.getTotalCardinality(tagName);
    }

    public Map<String, Long> getCardinality(String tagName) {
        return repository.getCardinality(tagName);
    }

    public Set<String> listTags() {
        return repository.listAllTags();
    }

    public void deleteTag(String tagName) {
        repository.deleteTag(tagName);
    }

    public ShardedQueryService.ShardedQueryResult query(String dsl)
            throws IOException, ExecutionException, InterruptedException {
        return queryService.query(dsl);
    }

    public ShardedQueryService.ShardedQueryResult query(String dsl, int offset, int limit)
            throws IOException, ExecutionException, InterruptedException {
        return queryService.query(dsl, offset, limit);
    }

    public int queryCount(String dsl) throws IOException, ExecutionException, InterruptedException {
        return queryService.queryCount(dsl);
    }

    public com.profile.store.optimizer.ExecutionPlan explain(String dsl) {
        return queryService.explain(dsl);
    }

    public ShardRebalanceService.RebalanceResult addShard(String host, int port, String password) throws Exception {
        ShardProperties.ShardNode newNode = new ShardProperties.ShardNode();
        newNode.setHost(host);
        newNode.setPort(port);
        if (password != null && !password.isEmpty()) {
            newNode.setPassword(password);
        }
        return rebalanceService.addShard(newNode);
    }

    public ShardRebalanceService.RebalanceStatus getRebalanceStatus() {
        return rebalanceService.getStatus();
    }

    public ShardHealthChecker.ShardHealthReport getHealthReport() {
        return healthChecker.getHealthReport();
    }

    public ShardProperties getProperties() {
        return properties;
    }

    public int getShardCount() {
        return repository.getShardRouter().getShardCount();
    }
}
