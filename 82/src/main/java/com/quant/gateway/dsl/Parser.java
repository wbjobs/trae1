package com.quant.gateway.dsl;

import com.quant.gateway.dsl.Lexer.Token;
import com.quant.gateway.dsl.TokenType;

import java.util.*;

/**
 * DSL 语法解析器 (递归下降)。
 *
 * 语法:
 *   rule          ::= selectExpr [ WHERE expr ]
 *   selectExpr    ::= aggregateExpr ( ',' aggregateExpr )*
 *   aggregateExpr ::= fnName '(' [ expr ] ')' [ alias IDENTIFIER ]
 *   expr          ::= orExpr
 *   orExpr        ::= andExpr ( 'or' andExpr )*
 *   andExpr       ::= notExpr ( 'and' notExpr )*
 *   notExpr       ::= 'not' notExpr | compareExpr
 *   compareExpr   ::= addExpr ( ( '=' | '!=' | '>' | '<' | '>=' | '<=' ) addExpr
 *   addExpr       ::= mulExpr ( ( '+' | '-' ) mulExpr )*
 *   mulExpr       ::= unaryExpr ( ( '*' | '/' | '%' ) unaryExpr )*
 *   unaryExpr     ::= '-' unaryExpr | atom
 *   atom          ::= NUMBER | STRING | TRUE | FALSE | NULL
 *                   | IDENTIFIER [ '.' IDENTIFIER ]*
 *                   | '(' expr ')'
 */
public final class Parser {

    private final List<Token> tokens;
    private int pos;

    public Parser(List<Token> tokens) {
        this.tokens = tokens;
    }

    /**
     * 解析完整规则, 返回聚合规则定义。
     * @return AggregateRuleDefinition
     */
    public AggregateRuleDefinition parseRule() {
        AggregateRuleDefinition def = new AggregateRuleDefinition();
        parseSelectExpr(def);
        if (peek().matches(TokenType.WHERE)) {
            consume(TokenType.WHERE);
            def.filter = parseOr();
        }
        expect(TokenType.EOF, "end of rule");
        return def;
    }

    /**
     * 仅解析过滤表达式 (用于 where 子句部分)。
     */
    public Expr parseFilter() {
        Expr e = parseOr();
        expect(TokenType.EOF, "end of filter");
        return e;
    }

    // ============ select 列表 ============

    private void parseSelectExpr(AggregateRuleDefinition def) {
        do {
            Expr.FunctionCall call = parseAggregateExpr();
            String alias = null;
            if (peek().matches(TokenType.IDENTIFIER)
            && !peek().matches(TokenType.WHERE)
            && !peek().matches(TokenType.COMMA)) {
                alias = consume(TokenType.IDENTIFIER).text;
            }
            def.metrics.add(new MetricDef(call, alias));
        } while (peek().matches(TokenType.COMMA) && consume(TokenType.COMMA) != null);
    }

    private Expr.FunctionCall parseAggregateExpr() {
        Token t = peek();
        Expr.FunctionCall.Fn fn;
        switch (t.type) {
            case FN_SUM:   fn = Expr.FunctionCall.Fn.SUM; break;
            case FN_AVG:   fn = Expr.FunctionCall.Fn.AVG; break;
            case FN_WAVG:  fn = Expr.FunctionCall.Fn.WAVG; break;
            case FN_COUNT: fn = Expr.FunctionCall.Fn.COUNT; break;
            case FN_MAX:   fn = Expr.FunctionCall.Fn.MAX; break;
            case FN_MIN:   fn = Expr.FunctionCall.Fn.MIN; break;
            case FN_FIRST: fn = Expr.FunctionCall.Fn.FIRST; break;
            case FN_LAST:  fn = Expr.FunctionCall.Fn.LAST; break;
            default:
                throw new ParseException("expected aggregate function, got: " + t, pos);
        }
        consume(t.type);
        consume(TokenType.LPAREN);
        List<Expr> args = new ArrayList<>();
        if (!peek().matches(TokenType.RPAREN)) {
            args.add(parseOr());
        }
        consume(TokenType.RPAREN);
        return new Expr.FunctionCall(fn, args);
    }

    // ============ 表达式 ============

    private Expr parseOr() {
        Expr left = parseAnd();
        while (peek().matches(TokenType.OR)) {
            consume(TokenType.OR);
            Expr right = parseAnd();
            left = new Expr.BinaryOp(Expr.BinaryOp.Op.OR, left, right);
        }
        return left;
    }

    private Expr parseAnd() {
        Expr left = parseNot();
        while (peek().matches(TokenType.AND)) {
            consume(TokenType.AND);
            Expr right = parseNot();
            left = new Expr.BinaryOp(Expr.BinaryOp.Op.AND, left, right);
        }
        return left;
    }

    private Expr parseNot() {
        if (peek().matches(TokenType.NOT)) {
            consume(TokenType.NOT);
            return new Expr.UnaryOp(Expr.UnaryOp.Op.NOT, parseNot());
        }
        return parseCompare();
    }

    private Expr parseCompare() {
        Expr left = parseAdd();
        Token t = peek();
        if (isCompareOp(t)) {
            consume(t.type);
            Expr right = parseAdd();
            Expr.BinaryOp.Op op = compareOp(t.type);
            return new Expr.BinaryOp(op, left, right);
        }
        return left;
    }

    private boolean isCompareOp(Token t) {
        return t.matches(TokenType.EQ) || t.matches(TokenType.NE)
            || t.matches(TokenType.GT) || t.matches(TokenType.LT)
            || t.matches(TokenType.GE) || t.matches(TokenType.LE);
    }

    private Expr.BinaryOp.Op compareOp(TokenType t) {
        switch (t) {
            case EQ: return Expr.BinaryOp.Op.EQ;
            case NE: return Expr.BinaryOp.Op.NE;
            case GT: return Expr.BinaryOp.Op.GT;
            case LT: return Expr.BinaryOp.Op.LT;
            case GE: return Expr.BinaryOp.Op.GE;
            case LE: return Expr.BinaryOp.Op.LE;
            default: throw new IllegalArgumentException("not compare op: " + t);
        }
    }

    private Expr parseAdd() {
        Expr left = parseMul();
        while (peek().matches(TokenType.PLUS) || peek().matches(TokenType.MINUS)) {
            Token t = consume(peek().type);
            Expr right = parseMul();
            Expr.BinaryOp.Op op = t.matches(TokenType.PLUS)
                    ? Expr.BinaryOp.Op.PLUS : Expr.BinaryOp.Op.MINUS;
            left = new Expr.BinaryOp(op, left, right);
        }
        return left;
    }

    private Expr parseMul() {
        Expr left = parseUnary();
        while (peek().matches(TokenType.STAR) || peek().matches(TokenType.SLASH) || peek().matches(TokenType.PERCENT)) {
            Token t = consume(peek().type);
            Expr right = parseUnary();
            Expr.BinaryOp.Op op;
            if (t.matches(TokenType.STAR)) op = Expr.BinaryOp.Op.STAR;
            else if (t.matches(TokenType.SLASH)) op = Expr.BinaryOp.Op.SLASH;
            else op = Expr.BinaryOp.Op.PERCENT;
            left = new Expr.BinaryOp(op, left, right);
        }
        return left;
    }

    private Expr parseUnary() {
        if (peek().matches(TokenType.MINUS)) {
            consume(TokenType.MINUS);
            return new Expr.UnaryOp(Expr.UnaryOp.Op.NEG, parseUnary());
        }
        return parseAtom();
    }

    private Expr parseAtom() {
        Token t = peek();
        if (t.matches(TokenType.NUMBER)) {
            consume(TokenType.NUMBER);
            return new Expr.NumberLiteral(t.asDouble());
        }
        if (t.matches(TokenType.STRING)) {
            consume(TokenType.STRING);
            return new Expr.StringLiteral(t.text);
        }
        if (t.matches(TokenType.TRUE)) {
            consume(TokenType.TRUE);
            return new Expr.BoolLiteral(true);
        }
        if (t.matches(TokenType.FALSE)) {
            consume(TokenType.FALSE);
            return new Expr.BoolLiteral(false);
        }
        if (t.matches(TokenType.NULL)) {
            consume(TokenType.NULL);
            return new Expr.BoolLiteral(false);
        }
        if (t.matches(TokenType.IDENTIFIER)) {
            consume(TokenType.IDENTIFIER);
            StringBuilder name = new StringBuilder(t.text);
            while (peek().matches(TokenType.DOT)) {
                consume(TokenType.DOT);
                name.append('.').append(consume(TokenType.IDENTIFIER).text);
            }
            return new Expr.FieldRef(name.toString());
        }
        if (t.matches(TokenType.LPAREN)) {
            consume(TokenType.LPAREN);
            Expr e = parseOr();
            consume(TokenType.RPAREN);
            return e;
        }
        throw new ParseException("unexpected token: " + t, pos);
    }

    // ============ 辅助 ============

    private Token peek() {
        return tokens.get(pos);
    }

    private Token consume(TokenType type) {
        Token t = tokens.get(pos);
        if (!t.matches(type)) {
            throw new ParseException("expected " + type + " but got " + t, pos);
        }
        pos++;
        return t;
    }

    private void expect(TokenType type, String msg) {
        if (!peek().matches(type)) {
            throw new ParseException("expected " + msg + " but got " + peek(), pos);
        }
    }

    /**
     * 解析异常。
     */
    public static final class ParseException extends RuntimeException {
        public final int position;
        public ParseException(String msg, int pos) {
            super(msg + " at position " + pos);
            this.position = pos;
        }
    }

    /**
     * 聚合规则定义。
     */
    public static final class AggregateRuleDefinition {
        public final List<MetricDef> metrics = new ArrayList<>();
        public Expr filter;

        public AggregateRuleDefinition() {}

        public boolean matches(com.quant.gateway.codec.TickData tick) {
            return filter == null || filter.evaluateBool(tick);
        }

        @Override
        public String toString() {
            return "RuleDef{metrics=" + metrics + ", filter=" + filter + "}";
        }
    }

    /**
     * 单个指标定义。
     */
    public static final class MetricDef {
        public final Expr.FunctionCall call;
        public final String alias;

        public MetricDef(Expr.FunctionCall call, String alias) {
            this.call = call;
            this.alias = alias;
        }

        public String name() {
            return alias != null ? alias : call.getFn().name().toLowerCase();
        }

        @Override
        public String toString() {
            return call + (alias != null ? " as " + alias : "");
        }
    }
}
