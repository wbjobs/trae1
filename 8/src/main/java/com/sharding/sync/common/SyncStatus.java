package com.sharding.sync.common;

import lombok.AllArgsConstructor;
import lombok.Getter;

@Getter
@AllArgsConstructor
public enum SyncStatus {

    PENDING("PENDING", "待执行"),
    RUNNING("RUNNING", "执行中"),
    SUCCESS("SUCCESS", "成功"),
    PARTIAL("PARTIAL", "部分成功"),
    FAILED("FAILED", "失败"),
    CANCELED("CANCELED", "已取消");

    private final String code;
    private final String desc;
}
