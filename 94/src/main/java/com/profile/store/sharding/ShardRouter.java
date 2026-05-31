package com.profile.store.sharding;

/**
 * 分片路由器：根据 user_id 计算目标分片。
 * <p>
 * 使用简单的一致性取模路由：shardId = userId mod shardCount。
 * 当分片数量变更时（扩容），需要调用 {@link ShardRebalanceService} 进行数据迁移。
 */
public class ShardRouter {

    private final ShardProperties properties;
    private int currentShardCount;

    public ShardRouter(ShardProperties properties) {
        this.properties = properties;
        this.currentShardCount = properties.getShardCount();
    }

    public int route(int userId) {
        return Math.floorMod(userId, currentShardCount);
    }

    public int route(long userId) {
        return Math.floorMod((int) (userId & 0x7FFFFFFF), currentShardCount);
    }

    public int getShardCount() {
        return currentShardCount;
    }

    public synchronized void updateShardCount(int newShardCount) {
        this.currentShardCount = newShardCount;
        properties.setShardCount(newShardCount);
    }

    public int[] allShardIds() {
        int[] ids = new int[currentShardCount];
        for (int i = 0; i < currentShardCount; i++) {
            ids[i] = i;
        }
        return ids;
    }

    public ShardProperties.ShardNode getNode(int shardId) {
        for (ShardProperties.ShardNode node : properties.getNodes()) {
            if (node.getShardId() == shardId) {
                return node;
            }
        }
        return null;
    }
}
