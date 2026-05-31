package com.sharding.sync.shard.algorithm;

public class RangeShardAlgorithm implements ShardAlgorithm {

    @Override
    public String name() {
        return "range-long";
    }

    @Override
    public int shard(Object shardValue, int shardCount) {
        if (shardValue == null || shardCount <= 0) {
            return 0;
        }
        long value;
        if (shardValue instanceof Number) {
            value = ((Number) shardValue).longValue();
        } else {
            try {
                value = Long.parseLong(shardValue.toString());
            } catch (NumberFormatException e) {
                return 0;
            }
        }
        long bound = 10000000L;
        long step = bound / shardCount;
        if (step <= 0) {
            return 0;
        }
        int idx = (int) (value / step);
        return Math.min(idx, shardCount - 1);
    }
}
