package com.sharding.sync.shard.algorithm;

public class HashShardAlgorithm implements ShardAlgorithm {

    @Override
    public String name() {
        return "hash-int";
    }

    @Override
    public int shard(Object shardValue, int shardCount) {
        if (shardValue == null) {
            return 0;
        }
        int hash = shardValue.toString().hashCode();
        return Math.abs(hash) % shardCount;
    }
}
