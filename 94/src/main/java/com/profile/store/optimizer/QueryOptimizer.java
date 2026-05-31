package com.profile.store.optimizer;

import com.profile.store.expression.Expr;
import com.profile.store.expression.NormalFormTransformer;
import com.profile.store.expression.OpType;
import com.profile.store.optimizer.ExecutionPlan.PlanNode;

import java.util.*;
import java.util.stream.Collectors;

/**
 * 查询优化器：将原始布尔表达式转换为最优执行计划。
 * <p>
 * 核心策略：
 * <ol>
 *   <li>同时生成 CNF 和 DNF 两种范式，比较中间结果峰值内存估算，选择较小者</li>
 *   <li>对每个 n 元 AND/OR 子句，按估算基数升序排列（小位图先运算）</li>
 *   <li>AND 节点：从最小位图开始求交，逐步压缩结果，降低中间结果</li>
 *   <li>OR 节点：按位图大小排序后逐批求并，避免一次性膨胀</li>
 *   <li>NOT 节点：记录为 NOT_CLAUSE，执行时通过全量 AND NOT 实现</li>
 * </ol>
 */
public class QueryOptimizer {

    private static final long UNIVERSE_SIZE = 1_000_000_000L;
    private static final int BATCH_SIZE = 10;

    private final NormalFormTransformer transformer = new NormalFormTransformer();

    public ExecutionPlan optimize(Expr original, Map<String, Long> tagCardinalities) {
        Expr nnf = transformer.toNNF(original);

        Expr cnf = transformer.toCNF(nnf);
        Expr dnf = transformer.toDNF(nnf);

        PlanNode cnfPlan = buildPlanNode(cnf, tagCardinalities);
        PlanNode dnfPlan = buildPlanNode(dnf, tagCardinalities);

        long cnfPeak = estimatePeakMemory(cnfPlan);
        long dnfPeak = estimatePeakMemory(dnfPlan);

        if (cnfPeak <= dnfPeak) {
            return new ExecutionPlan(cnfPlan, "CNF", cnfPeak);
        } else {
            return new ExecutionPlan(dnfPlan, "DNF", dnfPeak);
        }
    }

    private PlanNode buildPlanNode(Expr expr, Map<String, Long> tagCardinalities) {
        switch (expr.getType()) {
            case TAG:
                String name = ((Expr.Tag) expr).getName();
                long card = tagCardinalities.getOrDefault(name, UNIVERSE_SIZE / 2);
                return PlanNode.tag(name, card);

            case NOT:
                Expr.Not not = (Expr.Not) expr;
                PlanNode childPlan = buildPlanNode(not.getChild(), tagCardinalities);
                long notEstimate = UNIVERSE_SIZE - childPlan.getEstimatedCardinality();
                return PlanNode.not(childPlan, Math.max(0, notEstimate));

            case AND: {
                Expr.And and = (Expr.And) expr;
                List<PlanNode> children = new ArrayList<>();
                for (Expr child : and.getChildren()) {
                    children.add(buildPlanNode(child, tagCardinalities));
                }
                children.sort(Comparator.comparingLong(PlanNode::getEstimatedCardinality));
                long andEstimate = children.isEmpty() ? 0 : estimateAndCardinality(children);
                return PlanNode.and(children, andEstimate);
            }

            case OR: {
                Expr.Or or = (Expr.Or) expr;
                List<PlanNode> children = new ArrayList<>();
                for (Expr child : or.getChildren()) {
                    children.add(buildPlanNode(child, tagCardinalities));
                }
                children.sort(Comparator.comparingLong(PlanNode::getEstimatedCardinality));
                long orEstimate = children.isEmpty() ? 0 : estimateOrCardinality(children);
                return PlanNode.or(children, orEstimate);
            }

            default:
                throw new IllegalStateException("Unknown op type: " + expr.getType());
        }
    }

    private long estimateAndCardinality(List<PlanNode> children) {
        if (children.isEmpty()) return 0;
        long result = children.get(0).getEstimatedCardinality();
        for (int i = 1; i < children.size(); i++) {
            long next = children.get(i).getEstimatedCardinality();
            result = Math.min(result, (long) (result * next / (double) UNIVERSE_SIZE));
            result = Math.max(1, result);
        }
        return result;
    }

    private long estimateOrCardinality(List<PlanNode> children) {
        long result = 0;
        for (PlanNode child : children) {
            long c = child.getEstimatedCardinality();
            result += c - (long) (result * c / (double) UNIVERSE_SIZE);
            result = Math.min(UNIVERSE_SIZE, result);
        }
        return result;
    }

    private long estimatePeakMemory(PlanNode node) {
        return estimatePeakMemory(node, 0);
    }

    private long estimatePeakMemory(PlanNode node, long currentPeak) {
        long selfMemory = estimateMemory(node.getEstimatedCardinality());
        long peak = Math.max(currentPeak, selfMemory);

        switch (node.getType()) {
            case AND_CLAUSE:
            case OR_CLAUSE: {
                long childrenMemory = 0;
                for (PlanNode child : node.getChildren()) {
                    childrenMemory = Math.max(childrenMemory, estimatePeakMemory(child, peak));
                }
                peak = Math.max(peak, childrenMemory);
                int batchCount = (node.getChildren().size() + BATCH_SIZE - 1) / BATCH_SIZE;
                peak = Math.max(peak, selfMemory + estimateMemory(node.getChildren().get(0).getEstimatedCardinality()) * BATCH_SIZE);
                break;
            }
            case NOT_CLAUSE: {
                if (!node.getChildren().isEmpty()) {
                    peak = Math.max(peak, estimatePeakMemory(node.getChildren().get(0), peak));
                }
                break;
            }
            default:
                break;
        }
        return peak;
    }

    private long estimateMemory(long cardinality) {
        return cardinality * 4L;
    }
}
