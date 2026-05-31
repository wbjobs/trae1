package com.sharding.sync.shard.algorithm;

public class ModShardAlgorithm implements ShardAlgorithm {

    @Override
    public String name() {
        return "mod-long";
    }

    @Override
    public int shard(Object shardValue, int shardCount) {
        if (shardValue == null) {
            return 0;
        }
        long value;
        if (shardValue instanceof Number) {
            value = ((Number) shardValue).longValue();
        } else {
            try {
                value = Long.parseLong(shardValue.toString());
            } catch (NumberFormatException e) {
                value = shardValue.toString().hashCode() & 0x7fffffffL;
            }
        }
        return (int) (Math.abs(value) % shardCount);
    }
}
