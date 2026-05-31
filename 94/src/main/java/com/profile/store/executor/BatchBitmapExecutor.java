package com.profile.store.executor;

import com.profile.store.bitmap.BitmapSerde;
import com.profile.store.optimizer.ExecutionPlan;
import com.profile.store.optimizer.ExecutionPlan.PlanNode;
import org.roaringbitmap.RoaringBitmap;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * 批量位图运算执行器，支持临时文件溢出。
 * <p>
 * 核心设计：
 * <ul>
 *   <li>内存中最多同时持有 {@link #BATCH_SIZE} 个位图</li>
 *   <li>超出部分序列化写入临时文件，待需要时再反序列化回内存</li>
 *   <li>所有中间结果在计算完成后统一清理临时文件</li>
 *   <li>AND 运算按从小到大逐步求交，OR 运算按批处理并分批合并</li>
 * </ul>
 */
public class BatchBitmapExecutor implements Closeable {

    private static final int BATCH_SIZE = 10;
    private static final long MAX_IN_MEMORY_BYTES = 128L * 1024 * 1024;

    private final Map<String, RoaringBitmap> tagLoader;
    private final List<Path> tempFiles = new ArrayList<>();
    private final AtomicInteger tempSeq = new AtomicInteger(0);
    private final long universeMax;

    public BatchBitmapExecutor(Map<String, RoaringBitmap> tagLoader, long universeMax) {
        this.tagLoader = tagLoader;
        this.universeMax = universeMax;
    }

    public RoaringBitmap execute(ExecutionPlan plan) throws IOException {
        try {
            return evaluate(plan.getRoot());
        } finally {
            cleanup();
        }
    }

    private RoaringBitmap evaluate(PlanNode node) throws IOException {
        switch (node.getType()) {
            case TAG_LEAF:
                return loadTag(node.getTagName());

            case NOT_CLAUSE:
                RoaringBitmap child = evaluate(node.getChildren().get(0));
                return bitmapNot(child);

            case AND_CLAUSE:
                return evaluateAnd(node.getChildren());

            case OR_CLAUSE:
                return evaluateOr(node.getChildren());

            default:
                throw new IllegalStateException("Unknown plan node type: " + node.getType());
        }
    }

    private RoaringBitmap loadTag(String tagName) {
        RoaringBitmap bm = tagLoader.get(tagName);
        return bm != null ? bm : new RoaringBitmap();
    }

    private RoaringBitmap bitmapNot(RoaringBitmap source) {
        RoaringBitmap universe = new RoaringBitmap();
        universe.add(0L, universeMax + 1);
        return RoaringBitmap.andNot(universe, source);
    }

    private RoaringBitmap evaluateAnd(List<PlanNode> children) throws IOException {
        if (children.isEmpty()) return new RoaringBitmap();

        List<SpillableBitmap> operands = new ArrayList<>();
        try {
            for (PlanNode child : children) {
                RoaringBitmap bm = evaluate(child);
                operands.add(new SpillableBitmap(bm, estimateBytes(bm)));
            }

            operands.sort(Comparator.comparingLong(SpillableBitmap::estimatedBytes));

            RoaringBitmap result = operands.get(0).materialize();
            for (int i = 1; i < operands.size(); i++) {
                if (result.isEmpty()) break;
                RoaringBitmap next = operands.get(i).materialize();
                result.and(next);
                next.clear();
                if (estimateBytes(result) > MAX_IN_MEMORY_BYTES) {
                    SpillableBitmap spilled = SpillableBitmap.fromBitmap(result, tempSeq, tempFiles);
                    result.clear();
                    result = spilled.materialize();
                }
            }
            return result;
        } finally {
            for (SpillableBitmap op : operands) {
                op.dispose();
            }
        }
    }

    private RoaringBitmap evaluateOr(List<PlanNode> children) throws IOException {
        if (children.isEmpty()) return new RoaringBitmap();

        List<SpillableBitmap> operands = new ArrayList<>();
        try {
            for (PlanNode child : children) {
                RoaringBitmap bm = evaluate(child);
                operands.add(new SpillableBitmap(bm, estimateBytes(bm)));
            }

            operands.sort(Comparator.comparingLong(SpillableBitmap::estimatedBytes));

            List<SpillableBitmap> batches = new ArrayList<>();
            for (int i = 0; i < operands.size(); i += BATCH_SIZE) {
                int end = Math.min(i + BATCH_SIZE, operands.size());
                List<SpillableBitmap> batch = operands.subList(i, end);
                RoaringBitmap batchResult = batchOr(batch);
                batches.add(new SpillableBitmap(batchResult, estimateBytes(batchResult)));
            }

            while (batches.size() > 1) {
                List<SpillableBitmap> nextBatches = new ArrayList<>();
                for (int i = 0; i < batches.size(); i += BATCH_SIZE) {
                    int end = Math.min(i + BATCH_SIZE, batches.size());
                    List<SpillableBitmap> mergeBatch = batches.subList(i, end);
                    RoaringBitmap merged = batchOr(mergeBatch);
                    nextBatches.add(new SpillableBitmap(merged, estimateBytes(merged)));
                    for (SpillableBitmap b : mergeBatch) b.dispose();
                }
                batches = nextBatches;
            }

            return batches.isEmpty() ? new RoaringBitmap() : batches.get(0).materialize();
        } finally {
            for (SpillableBitmap op : operands) {
                op.dispose();
            }
        }
    }

    private RoaringBitmap batchOr(List<SpillableBitmap> batch) throws IOException {
        if (batch.isEmpty()) return new RoaringBitmap();
        RoaringBitmap result = batch.get(0).materialize();
        for (int i = 1; i < batch.size(); i++) {
            RoaringBitmap next = batch.get(i).materialize();
            result.or(next);
            next.clear();
        }
        return result;
    }

    private long estimateBytes(RoaringBitmap bitmap) {
        return (long) bitmap.getCardinality() * 4L;
    }

    private void cleanup() {
        for (Path p : tempFiles) {
            try {
                Files.deleteIfExists(p);
            } catch (IOException ignored) {
            }
        }
        tempFiles.clear();
    }

    @Override
    public void close() {
        cleanup();
    }

    /**
     * 可溢出的位图包装：当内存占用超阈值时序列化到临时文件。
     */
    private static class SpillableBitmap {
        private RoaringBitmap inMemory;
        private Path spilledFile;
        private long estimatedBytes;

        SpillableBitmap(RoaringBitmap bitmap, long estimatedBytes) {
            this.inMemory = bitmap;
            this.estimatedBytes = estimatedBytes;
        }

        static SpillableBitmap fromBitmap(RoaringBitmap bitmap, AtomicInteger seq, List<Path> tempFiles) throws IOException {
            SpillableBitmap s = new SpillableBitmap(null, (long) bitmap.getCardinality() * 4L);
            s.spill(bitmap, seq, tempFiles);
            return s;
        }

        long estimatedBytes() {
            return estimatedBytes;
        }

        RoaringBitmap materialize() throws IOException {
            if (inMemory != null) {
                return inMemory;
            }
            if (spilledFile != null) {
                byte[] data = Files.readAllBytes(spilledFile);
                inMemory = BitmapSerde.deserialize(data);
                return inMemory;
            }
            return new RoaringBitmap();
        }

        private void spill(RoaringBitmap bitmap, AtomicInteger seq, List<Path> tempFiles) throws IOException {
            byte[] data = BitmapSerde.serialize(bitmap);
            String prefix = "bitmap-tmp-" + seq.incrementAndGet() + "-";
            Path tmp = Files.createTempFile(prefix, ".rb");
            Files.write(tmp, data);
            this.spilledFile = tmp;
            this.inMemory = null;
            tempFiles.add(tmp);
        }

        void dispose() {
            if (inMemory != null) {
                inMemory.clear();
                inMemory = null;
            }
        }
    }
}
