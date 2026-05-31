package com.profile.store.sharding;

import com.profile.store.bitmap.BitmapSerde;
import com.profile.store.dsl.DslParser;
import com.profile.store.executor.BatchBitmapExecutor;
import com.profile.store.expression.Expr;
import com.profile.store.optimizer.ExecutionPlan;
import com.profile.store.optimizer.QueryOptimizer;
import org.roaringbitmap.RoaringBitmap;
import org.springframework.stereotype.Service;

import java.io.IOException;
import java.util.*;
import java.util.concurrent.*;

/**
 * 分片查询服务：并行查询所有分片，聚合并返回全局结果。
 * <p>
 * 查询流程：
 * <ol>
 *   <li>解析 DSL → Expr 表达式树</li>
 *   <li>收集所有涉及的标签名</li>
 *   <li>并行向每个活跃分片发起查询</li>
 *   <li>每个分片独立执行位图运算</li>
 *   <li>聚合所有分片的 RoaringBitmap 结果（OR合并）</li>
 *   <li>返回聚合后的 count / 用户列表</li>
 * </ol>
 */
@Service
public class ShardedQueryService {

    private static final long UNIVERSE_MAX = 1_000_000_000L;

    private final DslParser dslParser = new DslParser();
    private final QueryOptimizer queryOptimizer = new QueryOptimizer();
    private final ShardedTagBitmapRepository repository;
    private final ShardRouter shardRouter;
    private final ExecutorService queryExecutor;

    public ShardedQueryService(ShardedTagBitmapRepository repository, ShardProperties properties) {
        this.repository = repository;
        this.shardRouter = repository.getShardRouter();
        int parallelism = Math.max(properties.getParallelism(), 1);
        this.queryExecutor = Executors.newFixedThreadPool(parallelism,
                r -> {
                    Thread t = new Thread(r, "shard-query");
                    t.setDaemon(true);
                    return t;
                });
    }

    public ShardedQueryResult query(String dsl) throws IOException, ExecutionException, InterruptedException {
        return query(dsl, 0, Integer.MAX_VALUE);
    }

    public ShardedQueryResult query(String dsl, int offset, int limit)
            throws IOException, ExecutionException, InterruptedException {

        Expr expr = dslParser.parse(dsl);
        Set<String> tagNames = collectTagNames(expr);

        Map<String, Long> globalCardinalities = new HashMap<>();
        for (String tag : tagNames) {
            long total = 0;
            Map<Integer, Long> shardCards = repository.getCardinality(tag);
            for (Long c : shardCards.values()) {
                total += c;
            }
            globalCardinalities.put(tag, total);
        }

        ExecutionPlan plan = queryOptimizer.optimize(expr, globalCardinalities);

        int[] shardIds = shardRouter.allShardIds();
        List<Future<ShardResult>> futures = new ArrayList<>();

        for (int shardId : shardIds) {
            if (!repository.getConnectionFactory().isShardActive(shardId)) {
                continue;
            }
            futures.add(queryExecutor.submit(() -> queryShard(expr, tagNames, shardId)));
        }

        RoaringBitmap aggregated = new RoaringBitmap();
        long estimatedPeakMemory = 0;
        String executionForm = plan.getNormalizedForm();
        int activeShardCount = 0;

        for (Future<ShardResult> future : futures) {
            ShardResult sr = future.get();
            if (sr.bitmap != null && !sr.bitmap.isEmpty()) {
                aggregated.or(sr.bitmap);
                sr.bitmap.clear();
            }
            estimatedPeakMemory = Math.max(estimatedPeakMemory, sr.estimatedPeakMemory);
            activeShardCount++;
        }

        int totalCount = aggregated.getCardinality();
        int[] users = BitmapSerde.pageToArray(aggregated, offset, limit);
        aggregated.clear();

        return new ShardedQueryResult(
                totalCount,
                users,
                executionForm,
                estimatedPeakMemory,
                activeShardCount,
                shardIds.length
        );
    }

    public int queryCount(String dsl) throws IOException, ExecutionException, InterruptedException {
        return query(dsl).getTotalCount();
    }

    public ShardedQueryResult queryByExpression(Expr expr) throws IOException, ExecutionException, InterruptedException {
        return queryByExpression(expr, 0, Integer.MAX_VALUE);
    }

    public ShardedQueryResult queryByExpression(Expr expr, int offset, int limit)
            throws IOException, ExecutionException, InterruptedException {

        Set<String> tagNames = collectTagNames(expr);
        Map<String, Long> globalCardinalities = new HashMap<>();
        for (String tag : tagNames) {
            long total = 0;
            Map<Integer, Long> shardCards = repository.getCardinality(tag);
            for (Long c : shardCards.values()) {
                total += c;
            }
            globalCardinalities.put(tag, total);
        }
        ExecutionPlan plan = queryOptimizer.optimize(expr, globalCardinalities);

        int[] shardIds = shardRouter.allShardIds();
        List<Future<ShardResult>> futures = new ArrayList<>();

        for (int shardId : shardIds) {
            if (!repository.getConnectionFactory().isShardActive(shardId)) {
                continue;
            }
            futures.add(queryExecutor.submit(() -> queryShard(expr, tagNames, shardId)));
        }

        RoaringBitmap aggregated = new RoaringBitmap();
        long estimatedPeakMemory = 0;
        String executionForm = plan.getNormalizedForm();
        int activeShardCount = 0;

        for (Future<ShardResult> future : futures) {
            ShardResult sr = future.get();
            if (sr.bitmap != null && !sr.bitmap.isEmpty()) {
                aggregated.or(sr.bitmap);
                sr.bitmap.clear();
            }
            estimatedPeakMemory = Math.max(estimatedPeakMemory, sr.estimatedPeakMemory);
            activeShardCount++;
        }

        int totalCount = aggregated.getCardinality();
        int[] users = BitmapSerde.pageToArray(aggregated, offset, limit);
        aggregated.clear();

        return new ShardedQueryResult(
                totalCount,
                users,
                executionForm,
                estimatedPeakMemory,
                activeShardCount,
                shardIds.length
        );
    }

    public ExecutionPlan explain(String dsl) {
        Expr expr = dslParser.parse(dsl);
        Set<String> tagNames = collectTagNames(expr);
        Map<String, Long> globalCardinalities = new HashMap<>();
        for (String tag : tagNames) {
            long total = 0;
            Map<Integer, Long> shardCards = repository.getCardinality(tag);
            for (Long c : shardCards.values()) {
                total += c;
            }
            globalCardinalities.put(tag, total);
        }
        return queryOptimizer.optimize(expr, globalCardinalities);
    }

    private ShardResult queryShard(Expr expr, Set<String> tagNames, int shardId) {
        try {
            Map<String, Long> shardCards = new HashMap<>();
            for (String tag : tagNames) {
                Map<Integer, Long> cards = repository.getCardinality(tag);
                shardCards.put(tag, cards.getOrDefault(shardId, 0L));
            }

            ExecutionPlan shardPlan = queryOptimizer.optimize(expr, shardCards);

            Map<String, RoaringBitmap> tagLoader = new HashMap<>();
            for (String tag : tagNames) {
                repository.findByTagAndShard(tag, shardId)
                        .ifPresent(bm -> tagLoader.put(tag, bm));
            }

            try (BatchBitmapExecutor executor = new BatchBitmapExecutor(tagLoader, UNIVERSE_MAX)) {
                RoaringBitmap result = executor.execute(shardPlan);
                return new ShardResult(shardId, result, shardPlan.getEstimatedPeakMemoryBytes());
            }
        } catch (Exception e) {
            return new ShardResult(shardId, new RoaringBitmap(), 0);
        }
    }

    private Set<String> collectTagNames(Expr expr) {
        Set<String> names = new HashSet<>();
        collectTagsRecursive(expr, names);
        return names;
    }

    private void collectTagsRecursive(Expr expr, Set<String> names) {
        if (expr instanceof Expr.Tag) {
            names.add(((Expr.Tag) expr).getName());
        }
        for (Expr child : expr.getChildren()) {
            collectTagsRecursive(child, names);
        }
    }

    private static class ShardResult {
        final int shardId;
        final RoaringBitmap bitmap;
        final long estimatedPeakMemory;

        ShardResult(int shardId, RoaringBitmap bitmap, long estimatedPeakMemory) {
            this.shardId = shardId;
            this.bitmap = bitmap;
            this.estimatedPeakMemory = estimatedPeakMemory;
        }
    }

    public static class ShardedQueryResult {
        private final int totalCount;
        private final int[] users;
        private final String executionForm;
        private final long estimatedPeakMemoryBytes;
        private final int activeShardCount;
        private final int totalShardCount;

        public ShardedQueryResult(int totalCount, int[] users, String executionForm,
                                  long estimatedPeakMemoryBytes, int activeShardCount,
                                  int totalShardCount) {
            this.totalCount = totalCount;
            this.users = users;
            this.executionForm = executionForm;
            this.estimatedPeakMemoryBytes = estimatedPeakMemoryBytes;
            this.activeShardCount = activeShardCount;
            this.totalShardCount = totalShardCount;
        }

        public int getTotalCount() { return totalCount; }
        public int[] getUsers() { return users; }
        public String getExecutionForm() { return executionForm; }
        public long getEstimatedPeakMemoryBytes() { return estimatedPeakMemoryBytes; }
        public int getActiveShardCount() { return activeShardCount; }
        public int getTotalShardCount() { return totalShardCount; }
    }
}
