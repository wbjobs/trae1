package com.distributed.task.common.result;

import lombok.AllArgsConstructor;
import lombok.Getter;

@Getter
@AllArgsConstructor
public enum ResultCode {

    SUCCESS(200, "操作成功"),
    FAIL(500, "系统异常"),
    PARAM_ERROR(400, "参数错误"),
    NOT_FOUND(404, "资源不存在"),

    TASK_NOT_FOUND(1001, "任务不存在"),
    TASK_ALREADY_EXISTS(1002, "任务已存在"),
    TASK_STATUS_INVALID(1003, "任务状态不允许当前操作"),

    IDEMPOTENT_DUPLICATE(2001, "重复请求，已被幂等拦截"),

    RETRY_EXHAUSTED(3001, "重试次数已用尽"),
    RETRY_TASK_RUNNING(3002, "任务正在执行中"),

    ALARM_SEND_FAIL(4001, "告警发送失败");

    private final int code;
    private final String msg;
}
