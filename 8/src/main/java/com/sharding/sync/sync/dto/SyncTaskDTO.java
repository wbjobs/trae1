package com.sharding.sync.sync.dto;

import lombok.Data;

import javax.validation.constraints.NotBlank;
import java.util.Map;

@Data
public class SyncTaskDTO {

    @NotBlank(message = "逻辑表不能为空")
    private String logicTable;

    @NotBlank(message = "同步类型不能为空")
    private String syncType;

    private String sourceDs;

    private String targetDs;

    private String triggerMode = "MANUAL";

    private Map<String, Object> params;
}
