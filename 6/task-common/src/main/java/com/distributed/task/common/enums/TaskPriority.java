package com.distributed.task.common.enums;

import lombok.AllArgsConstructor;
import lombok.Getter;

@Getter
@AllArgsConstructor
public enum TaskPriority {

    HIGHEST(1, "最高"),
    HIGH(2, "高"),
    NORMAL(3, "普通"),
    LOW(4, "低"),
    LOWEST(5, "最低");

    private final int level;
    private final String desc;

    public static TaskPriority of(Integer level) {
        if (level == null) {
            return NORMAL;
        }
        for (TaskPriority p : values()) {
            if (p.level == level) {
                return p;
            }
        }
        return NORMAL;
    }

    public double toScore(long timestamp) {
        return (double) level * 1e15 + timestamp;
    }
}
