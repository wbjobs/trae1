package com.cdn.domain.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;
import java.time.LocalDateTime;

@Data
@TableName("cdn_alert_record")
public class AlertRecord {
    @TableId(type = IdType.AUTO)
    private Long id;
    private String resourceUrl;
    private String alertType;
    private String alertContent;
    private Integer alertLevel;
    private Integer handled;
    private String handleResult;
    private LocalDateTime createTime;
    private LocalDateTime handleTime;
}
