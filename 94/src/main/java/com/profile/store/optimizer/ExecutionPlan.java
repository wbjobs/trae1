package com.profile.store.optimizer;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

/**
 * 执行计划：描述每个逻辑子句的求值顺序，包含启发式排序信息。
 */
public class ExecutionPlan {

    public enum NodeType {
        AND_CLAUSE, OR_CLAUSE, NOT_CLAUSE, TAG_LEAF
    }

    public static class PlanNode {
        private final NodeType type;
        private final String tagName;
        private final List<PlanNode> children;
        private final long estimatedCardinality;

        private PlanNode(NodeType type, String tagName, List<PlanNode> children, long estimatedCardinality) {
            this.type = type;
            this.tagName = tagName;
            this.children = children;
            this.estimatedCardinality = estimatedCardinality;
        }

        public static PlanNode tag(String name, long cardinality) {
            return new PlanNode(NodeType.TAG_LEAF, name, List.of(), cardinality);
        }

        public static PlanNode and(List<PlanNode> children, long estimate) {
            return new PlanNode(NodeType.AND_CLAUSE, null, children, estimate);
        }

        public static PlanNode or(List<PlanNode> children, long estimate) {
            return new PlanNode(NodeType.OR_CLAUSE, null, children, estimate);
        }

        public static PlanNode not(PlanNode child, long estimate) {
            return new PlanNode(NodeType.NOT_CLAUSE, null, List.of(child), estimate);
        }

        public NodeType getType() { return type; }
        public String getTagName() { return tagName; }
        public List<PlanNode> getChildren() { return children; }
        public long getEstimatedCardinality() { return estimatedCardinality; }
    }

    private final PlanNode root;
    private final String normalizedForm;
    private final long estimatedPeakMemoryBytes;

    public ExecutionPlan(PlanNode root, String normalizedForm, long estimatedPeakMemoryBytes) {
        this.root = root;
        this.normalizedForm = normalizedForm;
        this.estimatedPeakMemoryBytes = estimatedPeakMemoryBytes;
    }

    public PlanNode getRoot() { return root; }
    public String getNormalizedForm() { return normalizedForm; }
    public long getEstimatedPeakMemoryBytes() { return estimatedPeakMemoryBytes; }
}
