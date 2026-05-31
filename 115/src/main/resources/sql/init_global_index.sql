-- =====================================================================
-- PolarDB Global Index Database Initialization Script
-- =====================================================================

-- 创建全局索引数据库
CREATE DATABASE IF NOT EXISTS global_index_db
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_bin;

USE global_index_db;

-- 创建全局索引表
CREATE TABLE IF NOT EXISTS t_global_index (
  global_id VARCHAR(128) NOT NULL COMMENT '全局唯一ID',
  shard_key VARCHAR(128) NOT NULL COMMENT '分片键',
  shard_id VARCHAR(64) NOT NULL COMMENT '分片ID',
  gmt_create DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  gmt_modified DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
  PRIMARY KEY (global_id),
  KEY idx_shard_key (shard_key),
  KEY idx_shard_id (shard_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin COMMENT='全局索引表';

-- 分片表示例（在每个分片数据库中创建）
-- 这是分片业务表的示例，根据实际业务调整
-- CREATE TABLE IF NOT EXISTS shard_db.t_order (
--   order_id BIGINT NOT NULL COMMENT '订单ID（全局唯一键）',
--   user_id BIGINT NOT NULL COMMENT '用户ID（分片键）',
--   order_amount DECIMAL(18,2) NOT NULL DEFAULT 0,
--   status TINYINT NOT NULL DEFAULT 0,
--   gmt_create DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
--   gmt_modified DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
--   PRIMARY KEY (order_id),
--   KEY idx_user_id (user_id)
-- ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin COMMENT='订单表';

-- =====================================================================
-- 权限设置（可选）
-- =====================================================================
-- CREATE USER IF NOT EXISTS 'canal'@'%' IDENTIFIED BY 'canal';
-- GRANT SELECT, REPLICATION SLAVE, REPLICATION CLIENT ON *.* TO 'canal'@'%';
-- FLUSH PRIVILEGES;
