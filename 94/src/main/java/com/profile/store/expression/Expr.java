package com.profile.store.expression;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

/**
 * 布尔表达式节点。
 * <p>
 * 支持四种节点类型：
 * <ul>
 *   <li>TAG: 叶子节点，代表一个标签位图</li>
 *   <li>AND: n元合取，children 为合取子句</li>
 *   <li>OR: n元析取，children 为析取子句</li>
 *   <li>NOT: 一元取反，children[0] 为取反子表达式</li>
 * </ul>
 */
public abstract class Expr {
    protected final OpType type;

    protected Expr(OpType type) {
        this.type = type;
    }

    public OpType getType() {
        return type;
    }

    public abstract List<Expr> getChildren();

    public abstract Expr deepCopy();

    public static Tag tag(String name) {
        return new Tag(name);
    }

    public static And and(Expr... children) {
        return new And(children);
    }

    public static Or or(Expr... children) {
        return new Or(children);
    }

    public static Not not(Expr child) {
        return new Not(child);
    }

    public static final class Tag extends Expr {
        private final String name;

        public Tag(String name) {
            super(OpType.TAG);
            this.name = Objects.requireNonNull(name);
        }

        public String getName() {
            return name;
        }

        @Override
        public List<Expr> getChildren() {
            return Collections.emptyList();
        }

        @Override
        public Expr deepCopy() {
            return new Tag(name);
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (!(o instanceof Tag)) return false;
            Tag tag = (Tag) o;
            return name.equals(tag.name);
        }

        @Override
        public int hashCode() {
            return name.hashCode();
        }

        @Override
        public String toString() {
            return name;
        }
    }

    public static final class Not extends Expr {
        private final Expr child;

        public Not(Expr child) {
            super(OpType.NOT);
            this.child = Objects.requireNonNull(child);
        }

        public Expr getChild() {
            return child;
        }

        @Override
        public List<Expr> getChildren() {
            return Collections.singletonList(child);
        }

        @Override
        public Expr deepCopy() {
            return new Not(child.deepCopy());
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (!(o instanceof Not)) return false;
            Not not = (Not) o;
            return child.equals(not.child);
        }

        @Override
        public int hashCode() {
            return ~child.hashCode();
        }

        @Override
        public String toString() {
            return "NOT(" + child + ")";
        }
    }

    public static abstract class NAry extends Expr {
        protected final List<Expr> children;

        protected NAry(OpType type, List<Expr> children) {
            super(type);
            this.children = new ArrayList<>(children);
        }

        protected NAry(OpType type, Expr... children) {
            super(type);
            this.children = new ArrayList<>(children.length);
            for (Expr e : children) this.children.add(e);
        }

        @Override
        public List<Expr> getChildren() {
            return Collections.unmodifiableList(children);
        }

        public List<Expr> getMutableChildren() {
            return children;
        }
    }

    public static final class And extends NAry {
        public And(List<Expr> children) {
            super(OpType.AND, children);
        }

        public And(Expr... children) {
            super(OpType.AND, children);
        }

        @Override
        public Expr deepCopy() {
            List<Expr> copied = new ArrayList<>(children.size());
            for (Expr e : children) copied.add(e.deepCopy());
            return new And(copied);
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (!(o instanceof And)) return false;
            And and = (And) o;
            return children.equals(and.children);
        }

        @Override
        public int hashCode() {
            return children.stream().mapToInt(Expr::hashCode).reduce(1, (a, b) -> a * 31 + b);
        }

        @Override
        public String toString() {
            return "AND(" + children + ")";
        }
    }

    public static final class Or extends NAry {
        public Or(List<Expr> children) {
            super(OpType.OR, children);
        }

        public Or(Expr... children) {
            super(OpType.OR, children);
        }

        @Override
        public Expr deepCopy() {
            List<Expr> copied = new ArrayList<>(children.size());
            for (Expr e : children) copied.add(e.deepCopy());
            return new Or(copied);
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (!(o instanceof Or)) return false;
            Or or = (Or) o;
            return children.equals(or.children);
        }

        @Override
        public int hashCode() {
            return children.stream().mapToInt(Expr::hashCode).reduce(2, (a, b) -> a * 31 + b);
        }

        @Override
        public String toString() {
            return "OR(" + children + ")";
        }
    }
}
