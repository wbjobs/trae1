package com.sharding.sync.shard.algorithm;

public interface ShardAlgorithm {

    String name();

    int shard(Object shardValue, int shardCount);
}
