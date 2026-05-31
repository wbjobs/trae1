package com.distributed.task.common.enums;

import lombok.AllArgsConstructor;
import lombok.Getter;

@Getter
@AllArgsConstructor
public enum RetryLevel {

    LEVEL_1(1, 60, "1分钟后重试"),
    LEVEL_2(2, 300, "5分钟后重试"),
    LEVEL_3(3, 900, "15分钟后重试"),
    LEVEL_4(4, 3600, "1小时后重试"),
    LEVEL_5(5, 10800, "3小时后重试");

    private final int level;
    private final int intervalSeconds;
    private final String desc;

    public static RetryLevel of(int level) {
        for (RetryLevel r : values()) {
            if (r.level == level) {
                return r;
            }
        }
        return null;
    }

    public static RetryLevel next(int currentLevel) {
        int next = currentLevel + 1;
        RetryLevel r = of(next);
        return r != null ? r : LEVEL_5;
    }
}
