package com.profile.store.expression;

import java.util.ArrayList;
import java.util.List;

/**
 * 布尔表达式范式转换器。
 * <p>
 * 支持将任意表达式转换为：
 * <ul>
 *   <li>否定范式 (NNF): 否定仅出现在叶子节点</li>
 *   <li>析取范式 (DNF): OR of ANDs</li>
 *   <li>合取范式 (CNF): AND of ORs</li>
 * </ul>
 * <p>
 * 转换后便于后续执行计划优化：
 * - 对于 AND 密集型查询选择 CNF，逐个子句求交逐步压缩结果
 * - 对于 OR 密集型查询选择 DNF，逐个子句求并并控制中间结果膨胀
 */
public class NormalFormTransformer {

    public Expr toNNF(Expr expr) {
        return pushNegationsInward(eliminateDoubleNegations(expr.deepCopy()));
    }

    public Expr toDNF(Expr expr) {
        Expr nnf = toNNF(expr);
        return convertToDNF(nnf);
    }

    public Expr toCNF(Expr expr) {
        Expr nnf = toNNF(expr);
        return convertToCNF(nnf);
    }

    private Expr eliminateDoubleNegations(Expr e) {
        switch (e.getType()) {
            case TAG:
                return e;
            case NOT:
                Expr.Not not = (Expr.Not) e;
                Expr inner = eliminateDoubleNegations(not.getChild());
                if (inner.getType() == OpType.NOT) {
                    return eliminateDoubleNegations(((Expr.Not) inner).getChild());
                }
                return new Expr.Not(inner);
            case AND:
                Expr.And and = (Expr.And) e;
                List<Expr> newAndChildren = new ArrayList<>();
                for (Expr child : and.getChildren()) {
                    newAndChildren.add(eliminateDoubleNegations(child));
                }
                return new Expr.And(newAndChildren);
            case OR:
                Expr.Or or = (Expr.Or) e;
                List<Expr> newOrChildren = new ArrayList<>();
                for (Expr child : or.getChildren()) {
                    newOrChildren.add(eliminateDoubleNegations(child));
                }
                return new Expr.Or(newOrChildren);
            default:
                throw new IllegalStateException();
        }
    }

    private Expr pushNegationsInward(Expr e) {
        if (e.getType() != OpType.NOT) {
            if (e instanceof Expr.NAry) {
                Expr.NAry nary = (Expr.NAry) e;
                List<Expr> newChildren = new ArrayList<>();
                for (Expr child : nary.getChildren()) {
                    newChildren.add(pushNegationsInward(child));
                }
                if (e.getType() == OpType.AND) {
                    return new Expr.And(newChildren);
                } else {
                    return new Expr.Or(newChildren);
                }
            }
            return e;
        }

        Expr.Not not = (Expr.Not) e;
        Expr child = not.getChild();

        switch (child.getType()) {
            case TAG:
                return e;
            case NOT:
                return pushNegationsInward(((Expr.Not) child).getChild());
            case AND:
                Expr.And and = (Expr.And) child;
                List<Expr> demorganOr = new ArrayList<>();
                for (Expr c : and.getChildren()) {
                    demorganOr.add(pushNegationsInward(new Expr.Not(c)));
                }
                return new Expr.Or(demorganOr);
            case OR:
                Expr.Or or = (Expr.Or) child;
                List<Expr> demorganAnd = new ArrayList<>();
                for (Expr c : or.getChildren()) {
                    demorganAnd.add(pushNegationsInward(new Expr.Not(c)));
                }
                return new Expr.And(demorganAnd);
            default:
                throw new IllegalStateException();
        }
    }

    private Expr convertToDNF(Expr e) {
        e = flatten(e);
        if (e.getType() == OpType.AND) {
            Expr.And and = (Expr.And) e;
            List<Expr> children = new ArrayList<>();
            for (Expr c : and.getChildren()) {
                children.add(convertToDNF(c));
            }
            return distributeANDoverOR(new Expr.And(children));
        } else if (e.getType() == OpType.OR) {
            Expr.Or or = (Expr.Or) e;
            List<Expr> children = new ArrayList<>();
            for (Expr c : or.getChildren()) {
                children.add(convertToDNF(c));
            }
            return flatten(new Expr.Or(children));
        }
        return e;
    }

    private Expr convertToCNF(Expr e) {
        e = flatten(e);
        if (e.getType() == OpType.OR) {
            Expr.Or or = (Expr.Or) e;
            List<Expr> children = new ArrayList<>();
            for (Expr c : or.getChildren()) {
                children.add(convertToCNF(c));
            }
            return distributeORoverAND(new Expr.Or(children));
        } else if (e.getType() == OpType.AND) {
            Expr.And and = (Expr.And) e;
            List<Expr> children = new ArrayList<>();
            for (Expr c : and.getChildren()) {
                children.add(convertToCNF(c));
            }
            return flatten(new Expr.And(children));
        }
        return e;
    }

    private Expr distributeANDoverOR(Expr e) {
        e = flatten(e);
        if (e.getType() != OpType.AND) return e;

        Expr.And and = (Expr.And) e;
        List<Expr> children = and.getMutableChildren();

        for (int i = 0; i < children.size(); i++) {
            Expr c = children.get(i);
            if (c.getType() == OpType.OR) {
                Expr.Or or = (Expr.Or) c;
                children.remove(i);
                Expr.And restAnd = new Expr.And(new ArrayList<>(children));
                List<Expr> distributed = new ArrayList<>();
                for (Expr orChild : or.getChildren()) {
                    List<Expr> newAndChildren = new ArrayList<>();
                    newAndChildren.add(orChild);
                    newAndChildren.addAll(restAnd.getChildren());
                    distributed.add(convertToDNF(new Expr.And(newAndChildren)));
                }
                return flatten(new Expr.Or(distributed));
            }
        }
        return e;
    }

    private Expr distributeORoverAND(Expr e) {
        e = flatten(e);
        if (e.getType() != OpType.OR) return e;

        Expr.Or or = (Expr.Or) e;
        List<Expr> children = or.getMutableChildren();

        for (int i = 0; i < children.size(); i++) {
            Expr c = children.get(i);
            if (c.getType() == OpType.AND) {
                Expr.And and = (Expr.And) c;
                children.remove(i);
                Expr.Or restOr = new Expr.Or(new ArrayList<>(children));
                List<Expr> distributed = new ArrayList<>();
                for (Expr andChild : and.getChildren()) {
                    List<Expr> newOrChildren = new ArrayList<>();
                    newOrChildren.add(andChild);
                    newOrChildren.addAll(restOr.getChildren());
                    distributed.add(convertToCNF(new Expr.Or(newOrChildren)));
                }
                return flatten(new Expr.And(distributed));
            }
        }
        return e;
    }

    public Expr flatten(Expr e) {
        if (e instanceof Expr.NAry) {
            Expr.NAry nary = (Expr.NAry) e;
            List<Expr> newChildren = new ArrayList<>();
            for (Expr child : nary.getChildren()) {
                Expr flattened = flatten(child);
                if (flattened.getType() == e.getType()) {
                    newChildren.addAll(((Expr.NAry) flattened).getChildren());
                } else {
                    newChildren.add(flattened);
                }
            }
            if (e.getType() == OpType.AND) {
                return new Expr.And(newChildren);
            } else {
                return new Expr.Or(newChildren);
            }
        }
        return e;
    }
}
