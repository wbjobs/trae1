package com.distributed.task.common.utils;

import cn.hutool.core.util.IdUtil;
import cn.hutool.core.util.StrUtil;

public class TaskNoGenerator {

    private static final String PREFIX = "TASK";

    public static String generate() {
        return PREFIX + "_" + System.currentTimeMillis() + "_" + IdUtil.fastSimpleUUID().substring(0, 8).toUpperCase();
    }

    public static String buildKey(String taskType, String bizKey) {
        return StrUtil.join(":", taskType, bizKey);
    }
}
