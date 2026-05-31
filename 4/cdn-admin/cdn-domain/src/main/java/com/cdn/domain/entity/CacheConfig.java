package com.cdn.domain.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;
import java.time.LocalDateTime;

@Data
@TableName("cdn_cache_config")
public class CacheConfig {
    @TableId(type = IdType.AUTO)
    private Long id;
    private String configKey;
    private String resourcePattern;
    private Integer ttlSeconds;
    private Integer cacheStrategy;
    private Integer status;
    private String remark;
    private LocalDateTime createTime;
    private LocalDateTime updateTime;
}
