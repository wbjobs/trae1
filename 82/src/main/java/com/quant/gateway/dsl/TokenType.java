package com.quant.gateway.dsl;

/**
 * 词法单元类型。
 */
public enum TokenType {
    // 数值
    NUMBER,
    STRING,
    IDENTIFIER,

    // 聚合函数
    FN_SUM, FN_AVG, FN_WAVG, FN_COUNT, FN_MAX, FN_MIN, FN_FIRST, FN_LAST,

    // 关键字
    WHERE, AND, OR, NOT, TRUE, FALSE, NULL,

    // 符号
    LPAREN, RPAREN, COMMA, DOT,
    EQ, NE, GT, LT, GE, LE,
    PLUS, MINUS, STAR, SLASH, PERCENT,

    EOF,
    ERROR
}
