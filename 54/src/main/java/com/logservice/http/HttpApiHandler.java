package com.logservice.http;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.logservice.config.ServiceConfig;
import com.logservice.model.AggregateResult;
import com.logservice.model.AggregateResult.BucketResult;
import com.logservice.model.SearchHit;
import com.logservice.model.SearchResult;
import com.logservice.storage.AggregationManager;
import com.logservice.storage.BlockManager;
import com.logservice.storage.IndexManager;
import com.logservice.storage.SearchService;
import io.netty.buffer.Unpooled;
import io.netty.channel.ChannelFutureListener;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.ChannelInboundHandlerAdapter;
import io.netty.handler.codec.http.*;
import io.netty.util.CharsetUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.Instant;
import java.util.*;

public class HttpApiHandler extends ChannelInboundHandlerAdapter {
    private static final Logger LOG = LoggerFactory.getLogger(HttpApiHandler.class);

    private final ServiceConfig config;
    private final BlockManager blockManager;
    private final IndexManager indexManager;
    private final SearchService searchService;
    private final AggregationManager aggregationManager;
    private final ObjectMapper mapper = new ObjectMapper();

    public HttpApiHandler(ServiceConfig config, BlockManager blockManager,
                           IndexManager indexManager, SearchService searchService,
                           AggregationManager aggregationManager) {
        this.config = config;
        this.blockManager = blockManager;
        this.indexManager = indexManager;
        this.searchService = searchService;
        this.aggregationManager = aggregationManager;
    }

    @Override
    public void channelRead(ChannelHandlerContext ctx, Object msg) {
        if (!(msg instanceof FullHttpRequest)) {
            ctx.fireChannelRead(msg);
            return;
        }
        FullHttpRequest req = (FullHttpRequest) msg;
        try {
            QueryStringDecoder qs = new QueryStringDecoder(req.uri());
            String path = qs.path();
            HttpMethod method = req.method();

            if (path.equals("/ingest") && method == HttpMethod.POST) {
                handleIngest(ctx, req);
            } else if (path.startsWith("/search") && method == HttpMethod.GET) {
                handleSearch(ctx, qs);
            } else if (path.startsWith("/aggregate") && method == HttpMethod.GET) {
                handleAggregate(ctx, qs);
            } else if (path.equals("/status") && method == HttpMethod.GET) {
                handleStatus(ctx);
            } else if (path.equals("/flush") && method == HttpMethod.POST) {
                handleFlush(ctx);
            } else {
                writeError(ctx, HttpResponseStatus.NOT_FOUND, "not found: " + path);
            }
        } catch (Exception e) {
            LOG.error("Request handling error", e);
            writeError(ctx, HttpResponseStatus.INTERNAL_SERVER_ERROR, e.getMessage());
        } finally {
            req.release();
        }
    }

    private void handleIngest(ChannelHandlerContext ctx, FullHttpRequest req) throws Exception {
        String body = req.content().toString(CharsetUtil.UTF_8);
        String contentType = req.headers().get(HttpHeaderNames.CONTENT_TYPE, "text/plain");
        List<String> lines = parseLines(body, contentType);
        if (lines.isEmpty()) {
            writeJson(ctx, HttpResponseStatus.BAD_REQUEST,
                    Map.of("ok", false, "error", "empty body"));
            return;
        }
        blockManager.append(lines);
        Map<String, Object> resp = new LinkedHashMap<>();
        resp.put("ok", true);
        resp.put("count", lines.size());
        resp.put("totalLines", indexManager.getTotalLines());
        resp.put("currentBlockId", blockManager.getCurrentBlockId());
        writeJson(ctx, HttpResponseStatus.OK, resp);
    }

    private List<String> parseLines(String body, String contentType) {
        List<String> lines = new ArrayList<>();
        if (body == null) return lines;
        boolean isJson = contentType != null && contentType.toLowerCase().contains("application/json");
        if (!isJson) {
            String trimmed = body.trim();
            isJson = trimmed.startsWith("[") || trimmed.startsWith("{");
        }
        if (isJson) {
            try {
                Object o = mapper.readValue(body, Object.class);
                if (o instanceof List) {
                    for (Object item : (List<?>) o) {
                        if (item != null) lines.add(item.toString());
                    }
                } else if (o instanceof Map) {
                    Object linesObj = ((Map<?, ?>) o).get("lines");
                    if (linesObj instanceof List) {
                        for (Object item : (List<?>) linesObj) {
                            if (item != null) lines.add(item.toString());
                        }
                    } else {
                        lines.add(mapper.writeValueAsString(o));
                    }
                } else {
                    lines.add(o == null ? "" : o.toString());
                }
            } catch (Exception e) {
                for (String line : body.split("\n")) {
                    if (!line.isEmpty()) lines.add(line);
                }
            }
        } else {
            for (String line : body.split("\n")) {
                if (!line.isEmpty()) lines.add(line);
            }
        }
        return lines;
    }

    private void handleSearch(ChannelHandlerContext ctx, QueryStringDecoder qs) throws Exception {
        List<String> q = qs.parameters().get("q");
        List<String> limit = qs.parameters().get("limit");
        List<String> cursorParam = qs.parameters().get("cursor");
        if (q == null || q.isEmpty() || q.get(0).isEmpty()) {
            writeJson(ctx, HttpResponseStatus.BAD_REQUEST, Map.of("ok", false, "error", "missing q"));
            return;
        }
        int maxHits = 100;
        if (limit != null && !limit.isEmpty()) {
            try { maxHits = Integer.parseInt(limit.get(0)); } catch (NumberFormatException ignored) {}
        }
        long cursor = 0L;
        if (cursorParam != null && !cursorParam.isEmpty()) {
            try { cursor = Long.parseLong(cursorParam.get(0)); } catch (NumberFormatException ignored) {}
        }
        SearchResult result = searchService.search(q.get(0), cursor, maxHits);

        List<Map<String, Object>> hitList = new ArrayList<>(result.getHits().size());
        for (SearchHit h : result.getHits()) {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("lineId", h.getLineId());
            m.put("timestamp", h.getTimestamp());
            m.put("line", h.getLine());
            m.put("contextBefore", h.getContextBefore());
            m.put("contextAfter", h.getContextAfter());
            hitList.add(m);
        }
        Map<String, Object> resp = new LinkedHashMap<>();
        resp.put("ok", true);
        resp.put("query", q.get(0));
        resp.put("hits", hitList.size());
        resp.put("hasMore", result.isHasMore());
        if (result.getNextCursor() != null) resp.put("nextCursor", result.getNextCursor());
        resp.put("truncated", result.isTruncated());
        if (result.getTruncatedReason() != null) resp.put("truncatedReason", result.getTruncatedReason());
        resp.put("elapsedMs", result.getElapsedMs());
        resp.put("results", hitList);
        writeJson(ctx, HttpResponseStatus.OK, resp);
    }

    private void handleAggregate(ChannelHandlerContext ctx, QueryStringDecoder qs) throws Exception {
        List<String> fromParam = qs.parameters().get("from");
        List<String> toParam = qs.parameters().get("to");
        List<String> bucketParam = qs.parameters().get("bucket");

        long now = System.currentTimeMillis();
        long fromMs = now - 60_000L;
        long toMs = now;

        if (fromParam != null && !fromParam.isEmpty()) {
            fromMs = parseTimeParam(fromParam.get(0), now);
        }
        if (toParam != null && !toParam.isEmpty()) {
            toMs = parseTimeParam(toParam.get(0), now);
        }
        if (fromMs >= toMs) {
            writeJson(ctx, HttpResponseStatus.BAD_REQUEST,
                    Map.of("ok", false, "error", "from must be before to"));
            return;
        }

        String bucketSize = "1m";
        if (bucketParam != null && !bucketParam.isEmpty()) {
            bucketSize = bucketParam.get(0);
        }

        AggregateResult result = aggregationManager.query(fromMs, toMs, bucketSize);

        List<Map<String, Object>> bucketList = new ArrayList<>(result.getBuckets().size());
        for (BucketResult br : result.getBuckets()) {
            Map<String, Object> bm = new LinkedHashMap<>();
            bm.put("start", br.getStartMs());
            bm.put("startIso", Instant.ofEpochMilli(br.getStartMs()).toString());
            bm.put("end", br.getEndMs());
            bm.put("endIso", Instant.ofEpochMilli(br.getEndMs()).toString());
            bm.put("count", br.getCount());
            bm.put("levels", br.getLevels());
            bm.put("levelPercentages", br.getLevelPercentages());
            bm.put("modules", br.getModules());
            List<Map<String, Object>> tw = new ArrayList<>(br.getTopWords().size());
            for (Map.Entry<String, Long> e : br.getTopWords()) {
                Map<String, Object> wm = new LinkedHashMap<>();
                wm.put("word", e.getKey());
                wm.put("count", e.getValue());
                tw.add(wm);
            }
            bm.put("topWords", tw);
            bucketList.add(bm);
        }

        Map<String, Object> resp = new LinkedHashMap<>();
        resp.put("ok", true);
        resp.put("from", fromMs);
        resp.put("to", toMs);
        resp.put("bucketSize", result.getBucketSize());
        resp.put("truncated", result.isTruncated());
        if (result.isTruncated()) {
            resp.put("truncatedReason", "查询时间超过限制，结果不完整，请缩小时间范围");
        }
        resp.put("elapsedMs", result.getElapsedMs());
        resp.put("buckets", bucketList);
        writeJson(ctx, HttpResponseStatus.OK, resp);
    }

    private long parseTimeParam(String val, long now) {
        if (val.startsWith("now-")) {
            String rest = val.substring(4);
            long unitMs;
            if (rest.endsWith("h")) {
                unitMs = 3600_000L * Long.parseLong(rest.substring(0, rest.length() - 1));
            } else if (rest.endsWith("m")) {
                unitMs = 60_000L * Long.parseLong(rest.substring(0, rest.length() - 1));
            } else if (rest.endsWith("s")) {
                unitMs = 1000L * Long.parseLong(rest.substring(0, rest.length() - 1));
            } else if (rest.endsWith("d")) {
                unitMs = 86_400_000L * Long.parseLong(rest.substring(0, rest.length() - 1));
            } else {
                unitMs = Long.parseLong(rest);
            }
            return now - unitMs;
        }
        try {
            return Long.parseLong(val);
        } catch (NumberFormatException e) {
            return Instant.parse(val).toEpochMilli();
        }
    }

    private void handleStatus(ChannelHandlerContext ctx) throws Exception {
        Map<String, Object> resp = new LinkedHashMap<>();
        resp.put("ok", true);
        resp.put("totalLines", indexManager.getTotalLines());
        resp.put("termCount", indexManager.getTermCount());
        resp.put("blockCount", indexManager.getBlockCount());
        resp.put("currentBlockId", blockManager.getCurrentBlockId());
        resp.put("dataDir", config.getDataDir());
        resp.put("retentionDays", config.getRetentionDays());
        resp.put("blockSizeBytes", config.getBlockSizeBytes());
        resp.put("blockFlushIntervalMs", config.getBlockFlushIntervalMs());
        resp.put("searchTimeoutMs", config.getSearchTimeoutMs());
        resp.put("aggregationTimeoutMs", config.getAggregationTimeoutMs());
        resp.put("aggregationCompletedBuckets", aggregationManager.getCompletedBucketCount());
        resp.put("aggregationCurrentMinuteStart", aggregationManager.getCurrentMinuteStart());
        resp.put("aggregationCurrentMinuteCount", aggregationManager.getCurrentMinuteCount());
        writeJson(ctx, HttpResponseStatus.OK, resp);
    }

    private void handleFlush(ChannelHandlerContext ctx) throws Exception {
        blockManager.flushNow();
        Map<String, Object> resp = new LinkedHashMap<>();
        resp.put("ok", true);
        resp.put("totalLines", indexManager.getTotalLines());
        resp.put("blockCount", indexManager.getBlockCount());
        writeJson(ctx, HttpResponseStatus.OK, resp);
    }

    private void writeJson(ChannelHandlerContext ctx, HttpResponseStatus status, Object body) throws Exception {
        byte[] bytes = mapper.writeValueAsBytes(body);
        FullHttpResponse resp = new DefaultFullHttpResponse(
                HttpVersion.HTTP_1_1, status, Unpooled.copiedBuffer(bytes));
        resp.headers().set(HttpHeaderNames.CONTENT_TYPE, "application/json; charset=UTF-8");
        resp.headers().set(HttpHeaderNames.CONTENT_LENGTH, bytes.length);
        ctx.writeAndFlush(resp).addListener(ChannelFutureListener.CLOSE);
    }

    private void writeError(ChannelHandlerContext ctx, HttpResponseStatus status, String msg) {
        try {
            Map<String, Object> resp = new LinkedHashMap<>();
            resp.put("ok", false);
            resp.put("error", msg);
            writeJson(ctx, status, resp);
        } catch (Exception e) {
            ctx.close();
        }
    }

    @Override
    public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) {
        LOG.error("Channel error", cause);
        ctx.close();
    }
}
