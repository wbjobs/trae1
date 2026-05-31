package com.profile.store.dsl;

import com.profile.store.expression.Expr;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * 画像查询 DSL 解析器。
 * <p>
 * 支持语法：
 * <pre>
 *   标签名              - 单标签
 *   NOT 标签            - 取反
 *   标签1 AND 标签2      - 交集
 *   标签1 OR 标签2       - 并集
 *   (标签1 AND 标签2) OR 标签3  - 嵌套括号
 *   性别男 AND (年龄30-35 OR NOT 数码爱好者) - 复杂嵌套
 * </pre>
 * <p>
 * 标签名可包含中文、字母、数字、下划线、中划线。
 * 布尔运算符不区分大小写。
 */
public class DslParser {

    private static final Pattern TOKEN_PATTERN = Pattern.compile(
            "\\s*(?:(NOT|AND|OR|\\(|\\))|([\\u4e00-\\u9fa5a-zA-Z0-9_-]+))\\s*",
            Pattern.CASE_INSENSITIVE
    );

    public Expr parse(String dsl) {
        if (dsl == null || dsl.isBlank()) {
            throw new IllegalArgumentException("DSL 表达式不能为空");
        }
        List<Token> tokens = tokenize(dsl);
        ParserContext ctx = new ParserContext(tokens);
        Expr result = parseOr(ctx);
        if (ctx.hasMore()) {
            throw new DslParseException("意外的令牌: " + ctx.peek(), ctx.peek().position);
        }
        return result;
    }

    private List<Token> tokenize(String dsl) {
        List<Token> tokens = new ArrayList<>();
        Matcher m = TOKEN_PATTERN.matcher(dsl);
        int end = 0;
        while (m.find()) {
            if (m.start() != end) {
                String bad = dsl.substring(end, m.start()).trim();
                if (!bad.isEmpty()) {
                    throw new DslParseException("无法识别的字符: '" + bad + "'", end);
                }
            }
            String keyword = m.group(1);
            String literal = m.group(2);
            if (keyword != null) {
                tokens.add(new Token(TokenType.valueOf(keyword.toUpperCase()), keyword, m.start()));
            } else if (literal != null) {
                tokens.add(new Token(TokenType.TAG, literal, m.start()));
            }
            end = m.end();
        }
        if (end < dsl.length()) {
            String bad = dsl.substring(end).trim();
            if (!bad.isEmpty()) {
                throw new DslParseException("无法识别的字符: '" + bad + "'", end);
            }
        }
        return tokens;
    }

    private Expr parseOr(ParserContext ctx) {
        Expr left = parseAnd(ctx);
        while (ctx.hasMore() && ctx.peek().type == TokenType.OR) {
            ctx.advance();
            Expr right = parseAnd(ctx);
            left = Expr.or(left, right);
        }
        return left;
    }

    private Expr parseAnd(ParserContext ctx) {
        Expr left = parseNot(ctx);
        while (ctx.hasMore() && ctx.peek().type == TokenType.AND) {
            ctx.advance();
            Expr right = parseNot(ctx);
            left = Expr.and(left, right);
        }
        return left;
    }

    private Expr parseNot(ParserContext ctx) {
        if (ctx.hasMore() && ctx.peek().type == TokenType.NOT) {
            ctx.advance();
            Expr operand = parsePrimary(ctx);
            return Expr.not(operand);
        }
        return parsePrimary(ctx);
    }

    private Expr parsePrimary(ParserContext ctx) {
        if (!ctx.hasMore()) {
            throw new DslParseException("表达式意外结束", ctx.lastPosition());
        }
        Token token = ctx.peek();
        switch (token.type) {
            case LPAREN:
                ctx.advance();
                Expr inner = parseOr(ctx);
                if (!ctx.hasMore() || ctx.peek().type != TokenType.RPAREN) {
                    throw new DslParseException("缺少右括号", token.position);
                }
                ctx.advance();
                return inner;
            case TAG:
                ctx.advance();
                return Expr.tag(token.value);
            case RPAREN:
                throw new DslParseException("意外的右括号", token.position);
            default:
                throw new DslParseException("意外的令牌: " + token.value, token.position);
        }
    }

    private enum TokenType {
        TAG, AND, OR, NOT, LPAREN, RPAREN
    }

    private static class Token {
        final TokenType type;
        final String value;
        final int position;

        Token(TokenType type, String value, int position) {
            this.type = type;
            this.value = value;
            this.position = position;
        }

        @Override
        public String toString() {
            return type + "(" + value + ")";
        }
    }

    private static class ParserContext {
        private final List<Token> tokens;
        private int pos;

        ParserContext(List<Token> tokens) {
            this.tokens = tokens;
            this.pos = 0;
        }

        boolean hasMore() {
            return pos < tokens.size();
        }

        Token peek() {
            return tokens.get(pos);
        }

        Token advance() {
            return tokens.get(pos++);
        }

        int lastPosition() {
            return tokens.isEmpty() ? 0 : tokens.get(tokens.size() - 1).position;
        }
    }

    public static class DslParseException extends RuntimeException {
        private final int position;

        public DslParseException(String message, int position) {
            super(message + " [位置: " + position + "]");
            this.position = position;
        }

        public int getPosition() {
            return position;
        }
    }
}
