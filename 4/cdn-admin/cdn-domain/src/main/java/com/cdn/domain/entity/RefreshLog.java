package com.cdn.domain.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;
import java.time.LocalDateTime;

@Data
@TableName("cdn_refresh_log")
public class RefreshLog {
    @TableId(type = IdType.AUTO)
    private Long id;
    private String resourceUrl;
    private String refreshType;
    private Integer refreshStatus;
    private String operator;
    private String failReason;
    private Long costTime;
    private LocalDateTime createTime;
}
