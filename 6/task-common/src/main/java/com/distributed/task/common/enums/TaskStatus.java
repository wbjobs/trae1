package com.distributed.task.common.enums;

import lombok.AllArgsConstructor;
import lombok.Getter;

@Getter
@AllArgsConstructor
public enum TaskStatus {

    PENDING(0, "待执行"),
    RUNNING(1, "执行中"),
    SUCCESS(2, "执行成功"),
    FAILED(3, "执行失败"),
    RETRY_WAIT(4, "等待重试"),
    TIMEOUT(5, "执行超时"),
    CANCELLED(6, "已取消"),
    QUEUED(7, "已入队待领取");

    private final int code;
    private final String desc;

    public static TaskStatus of(int code) {
        for (TaskStatus s : values()) {
            if (s.code == code) {
                return s;
            }
        }
        return null;
    }
}
