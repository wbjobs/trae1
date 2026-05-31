package com.cdn.domain.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;
import java.time.LocalDateTime;

@Data
@TableName("cdn_resource")
public class CdnResource {
    @TableId(type = IdType.AUTO)
    private Long id;
    private String resourceUrl;
    private String resourceType;
    private String resourceName;
    private Long fileSize;
    private String etag;
    private Integer status;
    private String remark;
    private LocalDateTime createTime;
    private LocalDateTime updateTime;
}
