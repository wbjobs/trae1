package com.quant.gateway.dsl;

import com.quant.gateway.codec.TickData;

import java.util.*;

/**
 * DSL 表达式 AST 节点。
 *
 * 支持:
 *   - 字面量: 数字, 字符串, 布尔
 *   - 字段引用: price, volume, turnover, sector, symbol, tradeFlag, orderSide
 *   - 二元运算: + - * / % = != > < >= <= and or
 *   - 一元运算: not, - (取反)
 *   - 聚合函数调用: sum(x), avg(x), wavg(x), count(), max(x), min(x), first(x), last(x)
 *
 * 每个表达式可 evaluate 为一个值, 供聚合计算/过滤条件使用。
 */
public abstract class Expr {

    public abstract Object evaluate(TickData tick);

    public double evaluateDouble(TickData tick) {
        Object v = evaluate(tick);
        if (v == null) return 0.0;
        if (v instanceof Number) return ((Number) v).doubleValue();
        if (v instanceof Boolean) return ((Boolean) v) ? 1.0 : 0.0;
        try {
            return Double.parseDouble(v.toString());
        } catch (Exception e) {
            return 0.0;
        }
    }

    public boolean evaluateBool(TickData tick) {
        Object v = evaluate(tick);
        if (v == null) return false;
        if (v instanceof Boolean) return (Boolean) v;
        if (v instanceof Number) return ((Number) v).doubleValue() != 0;
        return Boolean.parseBoolean(v.toString());
    }

    // ============ 字面量 ============

    public static final class NumberLiteral extends Expr {
        final double value;
        public NumberLiteral(double v) { this.value = v; }
        @Override public Object evaluate(TickData tick) { return value; }
    }

    public static final class StringLiteral extends Expr {
        final String value;
        public StringLiteral(String v) { this.value = v; }
        @Override public Object evaluate(TickData tick) { return value; }
    }

    public static final class BoolLiteral extends Expr {
        final boolean value;
        public BoolLiteral(boolean v) { this.value = v; }
        @Override public Object evaluate(TickData tick) { return value; }
    }

    // ============ 字段引用 ============

    public static final class FieldRef extends Expr {
        final String name;
        public FieldRef(String name) { this.name = name; }
        @Override
        public Object evaluate(TickData tick) {
            switch (name.toLowerCase()) {
                case "price":     return tick.price;
                case "volume":    return tick.volume;
                case "turnover":  return tick.turnover;
                case "symbol":    return tick.symbol;
                case "sector":    return tick.sector;
                case "timestamp": return tick.timestamp;
                case "seq":       return tick.seq;
                case "tradeflag": return tick.tradeFlag;
                case "orderside": return tick.orderSide;
                default: return null;
            }
        }
    }

    // ============ 二元运算 ============

    public static final class BinaryOp extends Expr {
        public enum Op { PLUS, MINUS, STAR, SLASH, PERCENT, EQ, NE, GT, LT, GE, LE, AND, OR }
        final Op op;
        final Expr left;
        final Expr right;
        public BinaryOp(Op op, Expr left, Expr right) { this.op = op; this.left = left; this.right = right; }

        @Override
        public Object evaluate(TickData tick) {
            switch (op) {
                case PLUS:  return left.evaluateDouble(tick) + right.evaluateDouble(tick);
                case MINUS: return left.evaluateDouble(tick) - right.evaluateDouble(tick);
                case STAR:  return left.evaluateDouble(tick) * right.evaluateDouble(tick);
                case SLASH: {
                    double r = right.evaluateDouble(tick);
                    return r == 0 ? 0.0 : left.evaluateDouble(tick) / r;
                }
                case PERCENT: {
                    double r = right.evaluateDouble(tick);
                    return r == 0 ? 0.0 : left.evaluateDouble(tick) % r;
                }
                case EQ:  return Objects.equals(stringify(left.evaluate(tick)), stringify(right.evaluate(tick)));
                case NE:  return !Objects.equals(stringify(left.evaluate(tick)), stringify(right.evaluate(tick)));
                case GT:  return left.evaluateDouble(tick) > right.evaluateDouble(tick);
                case LT:  return left.evaluateDouble(tick) < right.evaluateDouble(tick);
                case GE:  return left.evaluateDouble(tick) >= right.evaluateDouble(tick);
                case LE:  return left.evaluateDouble(tick) <= right.evaluateDouble(tick);
                case AND: return left.evaluateBool(tick) && right.evaluateBool(tick);
                case OR:  return left.evaluateBool(tick) || right.evaluateBool(tick);
            }
            return null;
        }

        private static String stringify(Object o) {
            return o == null ? "" : o.toString();
        }
    }

    // ============ 一元运算 ============

    public static final class UnaryOp extends Expr {
        public enum Op { NEG, NOT }
        final Op op;
        final Expr operand;
        public UnaryOp(Op op, Expr operand) { this.op = op; this.operand = operand; }
        @Override
        public Object evaluate(TickData tick) {
            switch (op) {
                case NEG: return -operand.evaluateDouble(tick);
                case NOT: return !operand.evaluateBool(tick);
            }
            return null;
        }
    }

    // ============ 聚合函数 (在滑动窗口中调用) ============

    public static final class FunctionCall extends Expr {
        public enum Fn { SUM, AVG, WAVG, COUNT, MAX, MIN, FIRST, LAST }
        final Fn fn;
        final List<Expr> args;
        public FunctionCall(Fn fn, List<Expr> args) { this.fn = fn; this.args = args; }

        @Override public Object evaluate(TickData tick) {
            // 函数在 tick 级别的求值只返回当前 tick 的目标字段值,
            // 真正的聚合由 SlidingWindowAggregator 按窗口完成。
            Expr arg = args.isEmpty() ? null : args.get(0);
            switch (fn) {
                case COUNT: return 1L;
                case SUM:
                case AVG:
                case WAVG:
                case MAX:
                case MIN:
                case FIRST:
                case LAST:
                    return arg == null ? null : arg.evaluate(tick);
            }
            return null;
        }

        public Fn getFn() { return fn; }
        public List<Expr> getArgs() { return args; }
    }
}
