package com.profile.store.api;

import com.profile.store.dto.QueryRequest;
import com.profile.store.dto.ShardAddRequest;
import com.profile.store.dto.TagUpdateRequest;
import com.profile.store.service.TagService;
import com.profile.store.sharding.ShardHealthChecker;
import com.profile.store.sharding.ShardRebalanceService;
import com.profile.store.sharding.ShardedQueryService;
import jakarta.validation.Valid;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.*;

@RestController
@RequestMapping("/api/profile")
public class ProfileController {

    private final TagService tagService;

    public ProfileController(TagService tagService) {
        this.tagService = tagService;
    }

    @PostMapping("/query")
    public ResponseEntity<Map<String, Object>> query(@Valid @RequestBody QueryRequest request) {
        try {
            ShardedQueryService.ShardedQueryResult result = tagService.query(
                    request.getDsl(), request.getOffset(), request.getLimit());
            Map<String, Object> response = new LinkedHashMap<>();
            response.put("success", true);
            response.put("totalCount", result.getTotalCount());
            response.put("users", result.getUsers());
            response.put("executionForm", result.getExecutionForm());
            response.put("estimatedPeakMemoryBytes", result.getEstimatedPeakMemoryBytes());
            response.put("activeShardCount", result.getActiveShardCount());
            response.put("totalShardCount", result.getTotalShardCount());
            return ResponseEntity.ok(response);
        } catch (Exception e) {
            return ResponseEntity.badRequest().body(errorResponse(e));
        }
    }

    @PostMapping("/count")
    public ResponseEntity<Map<String, Object>> count(@Valid @RequestBody QueryRequest request) {
        try {
            int count = tagService.queryCount(request.getDsl());
            Map<String, Object> response = new LinkedHashMap<>();
            response.put("success", true);
            response.put("count", count);
            return ResponseEntity.ok(response);
        } catch (Exception e) {
            return ResponseEntity.badRequest().body(errorResponse(e));
        }
    }

    @PostMapping("/explain")
    public ResponseEntity<Map<String, Object>> explain(@Valid @RequestBody QueryRequest request) {
        try {
            var plan = tagService.explain(request.getDsl());
            Map<String, Object> response = new LinkedHashMap<>();
            response.put("success", true);
            response.put("normalizedForm", plan.getNormalizedForm());
            response.put("estimatedPeakMemoryBytes", plan.getEstimatedPeakMemoryBytes());
            response.put("rootNode", describeNode(plan.getRoot()));
            return ResponseEntity.ok(response);
        } catch (Exception e) {
            return ResponseEntity.badRequest().body(errorResponse(e));
        }
    }

    @GetMapping("/tags")
    public ResponseEntity<Map<String, Object>> listTags() {
        Set<String> tags = tagService.listTags();
        Map<String, Object> response = new LinkedHashMap<>();
        response.put("success", true);
        response.put("tags", tags);
        response.put("total", tags.size());
        return ResponseEntity.ok(response);
    }

    @GetMapping("/tags/{tagName}")
    public ResponseEntity<Map<String, Object>> getTagInfo(@PathVariable String tagName) {
        Map<String, Long> cardinality = tagService.getCardinality(tagName);
        long total = tagService.getTagCount(tagName);
        Map<String, Object> response = new LinkedHashMap<>();
        response.put("success", true);
        response.put("tag", tagName);
        response.put("totalCount", total);
        response.put("shardCardinality", cardinality);
        return ResponseEntity.ok(response);
    }

    @PostMapping("/tags/add")
    public ResponseEntity<Map<String, Object>> addUsers(@Valid @RequestBody TagUpdateRequest request) {
        try {
            tagService.addUsers(request.getTag(), request.getUserIds());
            Map<String, Object> response = new LinkedHashMap<>();
            response.put("success", true);
            response.put("tag", request.getTag());
            response.put("addedCount", request.getUserIds().size());
            return ResponseEntity.ok(response);
        } catch (Exception e) {
            return ResponseEntity.badRequest().body(errorResponse(e));
        }
    }

    @PostMapping("/tags/remove")
    public ResponseEntity<Map<String, Object>> removeUsers(@Valid @RequestBody TagUpdateRequest request) {
        try {
            tagService.removeUsers(request.getTag(), request.getUserIds());
            Map<String, Object> response = new LinkedHashMap<>();
            response.put("success", true);
            response.put("tag", request.getTag());
            response.put("removedCount", request.getUserIds().size());
            return ResponseEntity.ok(response);
        } catch (Exception e) {
            return ResponseEntity.badRequest().body(errorResponse(e));
        }
    }

    @DeleteMapping("/tags/{tagName}")
    public ResponseEntity<Map<String, Object>> deleteTag(@PathVariable String tagName) {
        try {
            tagService.deleteTag(tagName);
            Map<String, Object> response = new LinkedHashMap<>();
            response.put("success", true);
            response.put("tag", tagName);
            return ResponseEntity.ok(response);
        } catch (Exception e) {
            return ResponseEntity.badRequest().body(errorResponse(e));
        }
    }

    @GetMapping("/shards")
    public ResponseEntity<Map<String, Object>> listShards() {
        ShardRebalanceService.RebalanceStatus status = tagService.getRebalanceStatus();
        ShardHealthChecker.ShardHealthReport health = tagService.getHealthReport();
        Map<String, Object> response = new LinkedHashMap<>();
        response.put("success", true);
        response.put("currentShardCount", status.getCurrentShardCount());
        response.put("configuredNodeCount", status.getConfiguredNodeCount());
        response.put("rebalancing", status.isRebalancing());
        response.put("shardStatus", status.getShardStatus());
        response.put("activeCount", health.getActiveCount());
        response.put("inactiveCount", health.getInactiveCount());
        response.put("sentinelEnabled", health.isSentinelEnabled());
        response.put("currentMasters", health.getCurrentMasters());
        return ResponseEntity.ok(response);
    }

    @PostMapping("/shards/add")
    public ResponseEntity<Map<String, Object>> addShard(@Valid @RequestBody ShardAddRequest request) {
        try {
            ShardRebalanceService.RebalanceResult result = tagService.addShard(
                    request.getHost(), request.getPort(), request.getPassword());
            Map<String, Object> response = new LinkedHashMap<>();
            response.put("success", result.isSuccess());
            response.put("oldShardCount", result.getOldShardCount());
            response.put("newShardCount", result.getNewShardCount());
            response.put("migratedUsers", result.getMigratedUsers());
            response.put("skippedUsers", result.getSkippedUsers());
            response.put("processedTags", result.getProcessedTags());
            if (!result.getErrors().isEmpty()) {
                response.put("errors", result.getErrors());
            }
            return ResponseEntity.ok(response);
        } catch (Exception e) {
            return ResponseEntity.badRequest().body(errorResponse(e));
        }
    }

    @GetMapping("/health")
    public ResponseEntity<Map<String, Object>> health() {
        ShardHealthChecker.ShardHealthReport health = tagService.getHealthReport();
        Map<String, Object> response = new LinkedHashMap<>();
        response.put("success", true);
        response.put("healthy", health.isHealthy());
        response.put("activeCount", health.getActiveCount());
        response.put("inactiveCount", health.getInactiveCount());
        response.put("sentinelEnabled", health.isSentinelEnabled());
        response.put("shardStatus", health.getShardStatus());
        return ResponseEntity.ok(health.isHealthy() ? ResponseEntity.ok(response).getBody() == null
                ? ResponseEntity.ok(response) : ResponseEntity.ok(response)
                : ResponseEntity.status(503).body(response));
    }

    private Map<String, Object> errorResponse(Exception e) {
        Map<String, Object> error = new LinkedHashMap<>();
        error.put("success", false);
        error.put("error", e.getMessage());
        error.put("type", e.getClass().getSimpleName());
        return error;
    }

    private Map<String, Object> describeNode(com.profile.store.optimizer.ExecutionPlan.PlanNode node) {
        Map<String, Object> desc = new LinkedHashMap<>();
        desc.put("type", node.getType());
        if (node.getTagName() != null) {
            desc.put("tagName", node.getTagName());
        }
        desc.put("estimatedCardinality", node.getEstimatedCardinality());
        if (!node.getChildren().isEmpty()) {
            List<Map<String, Object>> children = new ArrayList<>();
            for (var child : node.getChildren()) {
                children.add(describeNode(child));
            }
            desc.put("children", children);
        }
        return desc;
    }
}
