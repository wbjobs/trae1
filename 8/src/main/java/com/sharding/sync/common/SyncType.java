package com.sharding.sync.common;

import lombok.AllArgsConstructor;
import lombok.Getter;

@Getter
@AllArgsConstructor
public enum SyncType {

    FULL("FULL", "全量同步"),
    INCREMENTAL("INCREMENTAL", "增量同步"),
    BINLOG("BINLOG", "Binlog 增量");

    private final String code;
    private final String desc;
}
