-- =====================================================================
-- 分布式任务幂等重试平台 数据库初始化脚本
-- Database : MySQL 8.0+
-- =====================================================================

CREATE DATABASE IF NOT EXISTS `task_platform`
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_general_ci;

USE `task_platform`;

-- ---------------------------------------------------------------------
-- 任务主表
-- ---------------------------------------------------------------------
DROP TABLE IF EXISTS `t_task_info`;
CREATE TABLE `t_task_info` (
    `id`                BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    `task_no`           VARCHAR(64)     NOT NULL COMMENT '任务编号',
    `task_type`         VARCHAR(64)     NOT NULL COMMENT '任务类型',
    `biz_key`           VARCHAR(128)    NOT NULL COMMENT '业务唯一键(幂等键)',
    `task_payload`      TEXT            NULL COMMENT '任务载荷(JSON)',
    `callback_url`      VARCHAR(512)    NULL COMMENT '回调URL',
    `status`            TINYINT         NOT NULL DEFAULT 0 COMMENT '状态 0待执行 1执行中 2成功 3失败 4重试等待 5超时 6取消 7已入队',
    `retry_count`       INT             NOT NULL DEFAULT 0 COMMENT '已重试次数',
    `max_retry_count`   INT             NOT NULL DEFAULT 5 COMMENT '最大重试次数',
    `retry_level`       TINYINT         NOT NULL DEFAULT 1 COMMENT '当前重试等级',
    `timeout_seconds`   INT             NOT NULL DEFAULT 300 COMMENT '超时秒数',
    `next_retry_time`   DATETIME        NULL COMMENT '下次重试时间',
    `first_execute_time` DATETIME       NULL COMMENT '首次执行时间',
    `last_execute_time` DATETIME        NULL COMMENT '最后执行时间',
    `owner_node`        VARCHAR(64)     NULL COMMENT '所属工作节点',
    `priority`          TINYINT         NOT NULL DEFAULT 3 COMMENT '优先级 1最高 2高 3普通 4低 5最低',
    `trace_id`          VARCHAR(32)     NULL COMMENT '链路追踪ID',
    `remark`            VARCHAR(512)    NULL COMMENT '备注/错误信息',
    `create_time`       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `update_time`       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    `deleted`           TINYINT         NOT NULL DEFAULT 0 COMMENT '逻辑删除 0否 1是',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_task_no` (`task_no`),
    UNIQUE KEY `uk_task_type_biz_key` (`task_type`, `biz_key`),
    KEY `idx_biz_key` (`task_type`, `biz_key`),
    KEY `idx_status_next_time` (`status`, `next_retry_time`),
    KEY `idx_owner_node` (`owner_node`),
    KEY `idx_priority_status` (`priority`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='任务主表';

-- ---------------------------------------------------------------------
-- 任务执行日志表
-- ---------------------------------------------------------------------
DROP TABLE IF EXISTS `t_task_log`;
CREATE TABLE `t_task_log` (
    `id`              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    `task_no`         VARCHAR(64)     NOT NULL COMMENT '任务编号',
    `task_type`       VARCHAR(64)     NULL COMMENT '任务类型',
    `execute_node`    VARCHAR(64)     NULL COMMENT '执行节点',
    `execute_status`  TINYINT         NOT NULL COMMENT '执行状态 1成功 0失败',
    `execute_result`  TEXT            NULL COMMENT '执行结果',
    `error_message`   VARCHAR(2000)   NULL COMMENT '错误信息',
    `cost_ms`         BIGINT          NULL COMMENT '耗时毫秒',
    `retry_no`        INT             NULL COMMENT '第几次重试',
    `create_time`     DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    KEY `idx_task_no` (`task_no`),
    KEY `idx_create_time` (`create_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='任务执行日志表';

-- ---------------------------------------------------------------------
-- 告警记录表
-- ---------------------------------------------------------------------
DROP TABLE IF EXISTS `t_alarm_record`;
CREATE TABLE `t_alarm_record` (
    `id`             BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    `alarm_no`       VARCHAR(64)     NOT NULL COMMENT '告警编号',
    `task_no`        VARCHAR(64)     NULL COMMENT '关联任务编号',
    `alarm_type`     VARCHAR(32)     NOT NULL COMMENT '告警类型 TASK_FAIL/TASK_TIMEOUT/SYSTEM',
    `alarm_level`    TINYINT         NOT NULL DEFAULT 3 COMMENT '告警等级 1-5',
    `alarm_content`  VARCHAR(2000)   NOT NULL COMMENT '告警内容',
    `receiver`       VARCHAR(128)    NULL COMMENT '接收人',
    `send_status`    TINYINT         NOT NULL DEFAULT 0 COMMENT '发送状态 1成功 0失败',
    `fail_reason`    VARCHAR(512)    NULL COMMENT '失败原因',
    `create_time`    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_alarm_no` (`alarm_no`),
    KEY `idx_task_no` (`task_no`),
    KEY `idx_create_time` (`create_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='告警记录表';

-- ---------------------------------------------------------------------
-- 重试策略表（动态配置）
-- ---------------------------------------------------------------------
DROP TABLE IF EXISTS `t_retry_strategy`;
CREATE TABLE `t_retry_strategy` (
    `id`                BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    `task_type`         VARCHAR(64)     NOT NULL COMMENT '任务类型',
    `max_retry_count`   INT             NOT NULL DEFAULT 5 COMMENT '最大重试次数',
    `retry_intervals`   VARCHAR(256)    NULL COMMENT '重试间隔秒数列表(逗号分隔)',
    `timeout_seconds`   INT             NOT NULL DEFAULT 300 COMMENT '任务超时秒数',
    `enabled`           TINYINT         NOT NULL DEFAULT 1 COMMENT '是否启用 1是 0否',
    `create_time`       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `update_time`       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_task_type` (`task_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='重试策略表';

-- 预置默认策略
INSERT INTO `t_retry_strategy` (`task_type`, `max_retry_count`, `retry_intervals`, `timeout_seconds`, `enabled`)
VALUES
    ('ORDER_NOTIFY', 5, '60,300,900,3600,10800', 300, 1),
    ('PAYMENT_CALLBACK', 5, '30,60,120,300,600', 120, 1),
    ('DEFAULT', 5, '60,300,900,3600,10800', 300, 1)
ON DUPLICATE KEY UPDATE `enabled` = VALUES(`enabled`);
