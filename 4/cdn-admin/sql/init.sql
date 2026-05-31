CREATE DATABASE IF NOT EXISTS cdn_admin DEFAULT CHARSET utf8mb4 COLLATE utf8mb4_general_ci;
USE cdn_admin;

DROP TABLE IF EXISTS cdn_resource;
CREATE TABLE cdn_resource (
    id BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    resource_url VARCHAR(500) NOT NULL COMMENT '资源URL',
    resource_type VARCHAR(50) COMMENT '资源类型 js/css/image/html',
    resource_name VARCHAR(200) COMMENT '资源名称',
    file_size BIGINT COMMENT '文件大小字节',
    etag VARCHAR(100) COMMENT 'ETag标识',
    status TINYINT DEFAULT 1 COMMENT '1正常 0异常 2已清理',
    remark VARCHAR(500) COMMENT '备注',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_url (resource_url(191)),
    KEY idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='CDN资源表';

DROP TABLE IF EXISTS cdn_cache_config;
CREATE TABLE cdn_cache_config (
    id BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    config_key VARCHAR(100) NOT NULL COMMENT '配置键',
    resource_pattern VARCHAR(500) NOT NULL COMMENT '资源匹配正则',
    ttl_seconds INT DEFAULT 3600 COMMENT '缓存过期时间秒',
    cache_strategy TINYINT DEFAULT 1 COMMENT '1标准 2热点 3冷资源',
    status TINYINT DEFAULT 1 COMMENT '1启用 0禁用',
    remark VARCHAR(500) COMMENT '备注',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_config_key (config_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='缓存配置表';

DROP TABLE IF EXISTS cdn_refresh_log;
CREATE TABLE cdn_refresh_log (
    id BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    resource_url VARCHAR(500) NOT NULL COMMENT '资源URL',
    refresh_type VARCHAR(20) COMMENT 'DELETE/UPDATE',
    refresh_status TINYINT COMMENT '1成功 0失败',
    operator VARCHAR(50) COMMENT '操作人',
    fail_reason VARCHAR(500) COMMENT '失败原因',
    cost_time BIGINT COMMENT '耗时毫秒',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    KEY idx_url (resource_url(191)),
    KEY idx_create_time (create_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='刷新日志表';

DROP TABLE IF EXISTS cdn_alert_record;
CREATE TABLE cdn_alert_record (
    id BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    resource_url VARCHAR(500) COMMENT '资源URL',
    alert_type VARCHAR(50) COMMENT '告警类型',
    alert_content VARCHAR(1000) COMMENT '告警内容',
    alert_level TINYINT COMMENT '1一般 2严重',
    handled TINYINT DEFAULT 0 COMMENT '0未处理 1已处理',
    handle_result VARCHAR(500) COMMENT '处理结果',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    handle_time DATETIME COMMENT '处理时间',
    KEY idx_handled (handled),
    KEY idx_create_time (create_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='告警记录表';

INSERT INTO cdn_cache_config (config_key, resource_pattern, ttl_seconds, cache_strategy, status, remark) VALUES
('static-img', '.*\\\\.(jpg|jpeg|png|gif|webp|svg)$', 86400, 2, 1, '图片资源热点策略'),
('static-js', '.*\\\\.js$', 3600, 1, 1, 'JS资源标准策略'),
('static-css', '.*\\\\.css$', 3600, 1, 1, 'CSS资源标准策略'),
('static-html', '.*\\\\.html$', 600, 3, 1, 'HTML冷资源策略');

INSERT INTO cdn_resource (resource_url, resource_type, resource_name, file_size, status, remark) VALUES
('https://cdn.example.com/static/js/app.js', 'js', 'app.js', 102400, 1, '主应用JS'),
('https://cdn.example.com/static/css/main.css', 'css', 'main.css', 51200, 1, '主样式'),
('https://cdn.example.com/static/img/logo.png', 'image', 'logo.png', 20480, 1, 'Logo图片'),
('https://cdn.example.com/static/img/banner.jpg', 'image', 'banner.jpg', 204800, 1, 'Banner图片'),
('https://cdn.example.com/static/html/index.html', 'html', 'index.html', 10240, 1, '首页HTML');
