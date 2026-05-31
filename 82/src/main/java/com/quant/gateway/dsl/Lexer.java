package com.quant.gateway.dsl;

import java.util.ArrayList;
import java.util.List;

/**
 * DSL 词法分析器。
 *
 * 支持的 Token:
 *   - 聚合函数: sum, avg, wavg, count, max, min, first, last
 *   - 关键字: where, and, or, not
 *   - 操作符: + - * / % = != > < >= <=
 *   - 标识符: price, volume, turnover, sector, symbol, tradeFlag, orderSide
 *   - 字面量: 数字, 字符串 ('xxx')
 *
 * 示例:
 *   sum(volume) where price > 100 and sector = 'tech'
 *   wavg(price) where volume > 10000
 *   count() where tradeFlag = 'B'
 */
public final class Lexer {

    private final String input;
    private int pos;
    private final int length;

    public Lexer(String input) {
        this.input = input;
        this.length = input.length();
    }

    public List<Token> tokenize() {
        List<Token> tokens = new ArrayList<>();
        while (pos < length) {
            skipWhitespace();
            if (pos >= length) break;
            char c = input.charAt(pos);

            if (Character.isDigit(c) || (c == '.' && pos + 1 < length && Character.isDigit(input.charAt(pos + 1)))) {
                tokens.add(readNumber());
            } else if (c == '\'') {
                tokens.add(readString());
            } else if (Character.isLetter(c) || c == '_') {
                tokens.add(readIdentifier());
            } else {
                tokens.add(readSymbol());
            }
        }
        tokens.add(new Token(TokenType.EOF, ""));
        return tokens;
    }

    private void skipWhitespace() {
        while (pos < length && Character.isWhitespace(input.charAt(pos))) {
            pos++;
        }
    }

    private Token readNumber() {
        int start = pos;
        boolean hasDot = false;
        while (pos < length) {
            char c = input.charAt(pos);
            if (Character.isDigit(c)) {
                pos++;
            } else if (c == '.' && !hasDot) {
                hasDot = true;
                pos++;
            } else {
                break;
            }
        }
        return new Token(TokenType.NUMBER, input.substring(start, pos));
    }

    private Token readString() {
        pos++; // skip '
        int start = pos;
        StringBuilder sb = new StringBuilder();
        while (pos < length) {
            char c = input.charAt(pos);
            if (c == '\'') {
                if (pos + 1 < length && input.charAt(pos + 1) == '\'') {
                    sb.append('\'');
                    pos += 2;
                    continue;
                }
                pos++;
                break;
            }
            sb.append(c);
            pos++;
        }
        return new Token(TokenType.STRING, sb.toString());
    }

    private Token readIdentifier() {
        int start = pos;
        while (pos < length) {
            char c = input.charAt(pos);
            if (Character.isLetterOrDigit(c) || c == '_') {
                pos++;
            } else {
                break;
            }
        }
        String word = input.substring(start, pos);
        TokenType type = keywordType(word);
        return new Token(type, word);
    }

    private TokenType keywordType(String word) {
        String lower = word.toLowerCase();
        switch (lower) {
            case "sum":   return TokenType.FN_SUM;
            case "avg":   return TokenType.FN_AVG;
            case "wavg":  return TokenType.FN_WAVG;
            case "count": return TokenType.FN_COUNT;
            case "max":   return TokenType.FN_MAX;
            case "min":   return TokenType.FN_MIN;
            case "first": return TokenType.FN_FIRST;
            case "last":  return TokenType.FN_LAST;
            case "where": return TokenType.WHERE;
            case "and":   return TokenType.AND;
            case "or":    return TokenType.OR;
            case "not":   return TokenType.NOT;
            case "true":  return TokenType.TRUE;
            case "false": return TokenType.FALSE;
            case "null":  return TokenType.NULL;
            default:      return TokenType.IDENTIFIER;
        }
    }

    private Token readSymbol() {
        char c = input.charAt(pos);
        pos++;
        switch (c) {
            case '(': return new Token(TokenType.LPAREN, "(");
            case ')': return new Token(TokenType.RPAREN, ")");
            case ',': return new Token(TokenType.COMMA, ",");
            case '.': return new Token(TokenType.DOT, ".");
            case '+': return new Token(TokenType.PLUS, "+");
            case '-': return new Token(TokenType.MINUS, "-");
            case '*': return new Token(TokenType.STAR, "*");
            case '/': return new Token(TokenType.SLASH, "/");
            case '%': return new Token(TokenType.PERCENT, "%");
            case '=': return new Token(TokenType.EQ, "=");
            case '!':
                if (pos < length && input.charAt(pos) == '=') {
                    pos++;
                    return new Token(TokenType.NE, "!=");
                }
                return new Token(TokenType.ERROR, "!");
            case '>':
                if (pos < length && input.charAt(pos) == '=') {
                    pos++;
                    return new Token(TokenType.GE, ">=");
                }
                return new Token(TokenType.GT, ">");
            case '<':
                if (pos < length && input.charAt(pos) == '=') {
                    pos++;
                    return new Token(TokenType.LE, "<=");
                }
                return new Token(TokenType.LT, "<");
            default:
                return new Token(TokenType.ERROR, String.valueOf(c));
        }
    }

    /**
     * Token 实例。
     */
    public static final class Token {
        public final TokenType type;
        public final String text;

        public Token(TokenType type, String text) {
            this.type = type;
            this.text = text;
        }

        public double asDouble() { return Double.parseDouble(text); }
        public long asLong() { return Long.parseLong(text); }
        public boolean matches(TokenType t) { return type == t; }

        @Override
        public String toString() {
            return type + "('" + text + "')";
        }
    }
}
