package com.sharding.sync.shard.algorithm;

import com.sharding.sync.common.BusinessException;
import org.springframework.stereotype.Component;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

@Component
public class ShardAlgorithmRegistry {

    private final Map<String, ShardAlgorithm> algorithms = new ConcurrentHashMap<>();

    public ShardAlgorithmRegistry() {
        register(new ModShardAlgorithm());
        register(new HashShardAlgorithm());
        register(new RangeShardAlgorithm());
    }

    public void register(ShardAlgorithm algorithm) {
        algorithms.put(algorithm.name(), algorithm);
    }

    public ShardAlgorithm get(String name) {
        ShardAlgorithm algorithm = algorithms.get(name);
        if (algorithm == null) {
            throw new BusinessException("不支持的分片算法: " + name);
        }
        return algorithm;
    }
}
