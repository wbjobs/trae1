package com.configcenter.server.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ConfigHistoryEntity {
    private ConfigKeyEntity key;
    private long version;
    private String content;
    private String operator;
    private long changedAt;
    private String changeType;
    private String diff;

    public static String typeCreate() { return "CREATE"; }
    public static String typeUpdate() { return "UPDATE"; }
    public static String typeRollback() { return "ROLLBACK"; }
    public static String typeDelete() { return "DELETE"; }
}
