package com.sharding.sync.common;

import lombok.AllArgsConstructor;
import lombok.Getter;

@Getter
@AllArgsConstructor
public enum ResultCode {

    SUCCESS(200, "操作成功"),
    FAIL(500, "操作失败"),
    PARAM_ERROR(400, "参数错误"),
    UNAUTHORIZED(401, "未授权"),
    FORBIDDEN(403, "禁止访问"),
    NOT_FOUND(404, "资源不存在"),

    SYNC_RUNNING(1001, "同步任务正在执行"),
    SYNC_NOT_FOUND(1002, "同步任务不存在"),
    SYNC_FAILED(1003, "同步任务失败"),

    SHARD_RULE_NOT_FOUND(2001, "分片规则不存在"),
    SHARD_RULE_INVALID(2002, "分片规则非法"),

    CHECK_DIFF_FOUND(3001, "发现数据差异"),
    CHECK_CONSISTENT(3002, "数据一致"),
    CHECK_FAILED(3003, "数据校验失败"),

    FIX_FAILED(4001, "修复任务失败"),
    FIX_PARTIAL(4002, "存在未修复差异");

    private final Integer code;
    private final String message;
}
