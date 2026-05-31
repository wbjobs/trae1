package com.profile.store.service;

import com.profile.store.bitmap.BitmapSerde;
import com.profile.store.dsl.DslParser;
import com.profile.store.executor.BatchBitmapExecutor;
import com.profile.store.expression.Expr;
import com.profile.store.optimizer.ExecutionPlan;
import com.profile.store.optimizer.QueryOptimizer;
import com.profile.store.storage.TagBitmapRepository;
import org.roaringbitmap.RoaringBitmap;
import org.springframework.stereotype.Service;

import java.io.IOException;
import java.util.*;

/**
 * 画像查询服务：整合 DSL 解析、执行计划优化、批量位图运算。
 * <p>
 * 使用流程：
 * <ol>
 *   <li>DSL 字符串 → Expr 表达式树</li>
 *   <li>Expr → CNF/DNF 范式，选择最优执行计划</li>
 *   <li>BatchBitmapExecutor 按计划执行，自动处理临时文件溢出</li>
 *   <li>返回 RoaringBitmap 结果</li>
 * </ol>
 */
@Service
public class ProfileQueryService {

    private static final long UNIVERSE_MAX = 1_000_000_000L;

    private final DslParser dslParser;
    private final QueryOptimizer queryOptimizer;
    private final TagBitmapRepository repository;

    public ProfileQueryService(TagBitmapRepository repository) {
        this.dslParser = new DslParser();
        this.queryOptimizer = new QueryOptimizer();
        this.repository = repository;
    }

    public QueryResult query(String dsl) throws IOException {
        return query(dsl, 0, Integer.MAX_VALUE);
    }

    public QueryResult query(String dsl, int offset, int limit) throws IOException {
        Expr expr = dslParser.parse(dsl);
        Set<String> tagNames = collectTagNames(expr);
        Map<String, Long> cardinalities = repository.getCardinalities(tagNames);

        ExecutionPlan plan = queryOptimizer.optimize(expr, cardinalities);

        Map<String, RoaringBitmap> tagLoader = new HashMap<>();
        for (String tag : tagNames) {
            repository.findByTag(tag).ifPresent(bm -> tagLoader.put(tag, bm));
        }

        try (BatchBitmapExecutor executor = new BatchBitmapExecutor(tagLoader, UNIVERSE_MAX)) {
            RoaringBitmap result = executor.execute(plan);
            int totalCount = result.getCardinality();
            int[] users = BitmapSerde.pageToArray(result, offset, limit);
            return new QueryResult(totalCount, users, plan.getNormalizedForm(), plan.getEstimatedPeakMemoryBytes());
        }
    }

    public int queryCount(String dsl) throws IOException {
        Expr expr = dslParser.parse(dsl);
        Set<String> tagNames = collectTagNames(expr);
        Map<String, Long> cardinalities = repository.getCardinalities(tagNames);

        ExecutionPlan plan = queryOptimizer.optimize(expr, cardinalities);

        Map<String, RoaringBitmap> tagLoader = new HashMap<>();
        for (String tag : tagNames) {
            repository.findByTag(tag).ifPresent(bm -> tagLoader.put(tag, bm));
        }

        try (BatchBitmapExecutor executor = new BatchBitmapExecutor(tagLoader, UNIVERSE_MAX)) {
            RoaringBitmap result = executor.execute(plan);
            return result.getCardinality();
        }
    }

    public QueryResult queryByExpression(Expr expr) throws IOException {
        return queryByExpression(expr, 0, Integer.MAX_VALUE);
    }

    public QueryResult queryByExpression(Expr expr, int offset, int limit) throws IOException {
        Set<String> tagNames = collectTagNames(expr);
        Map<String, Long> cardinalities = repository.getCardinalities(tagNames);

        ExecutionPlan plan = queryOptimizer.optimize(expr, cardinalities);

        Map<String, RoaringBitmap> tagLoader = new HashMap<>();
        for (String tag : tagNames) {
            repository.findByTag(tag).ifPresent(bm -> tagLoader.put(tag, bm));
        }

        try (BatchBitmapExecutor executor = new BatchBitmapExecutor(tagLoader, UNIVERSE_MAX)) {
            RoaringBitmap result = executor.execute(plan);
            int totalCount = result.getCardinality();
            int[] users = BitmapSerde.pageToArray(result, offset, limit);
            return new QueryResult(totalCount, users, plan.getNormalizedForm(), plan.getEstimatedPeakMemoryBytes());
        }
    }

    public ExecutionPlan explain(String dsl) {
        Expr expr = dslParser.parse(dsl);
        Set<String> tagNames = collectTagNames(expr);
        Map<String, Long> cardinalities = repository.getCardinalities(tagNames);
        return queryOptimizer.optimize(expr, cardinalities);
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

    public static class QueryResult {
        private final int totalCount;
        private final int[] users;
        private final String executionForm;
        private final long estimatedPeakMemoryBytes;

        public QueryResult(int totalCount, int[] users, String executionForm, long estimatedPeakMemoryBytes) {
            this.totalCount = totalCount;
            this.users = users;
            this.executionForm = executionForm;
            this.estimatedPeakMemoryBytes = estimatedPeakMemoryBytes;
        }

        public int getTotalCount() { return totalCount; }
        public int[] getUsers() { return users; }
        public String getExecutionForm() { return executionForm; }
        public long getEstimatedPeakMemoryBytes() { return estimatedPeakMemoryBytes; }
    }
}
