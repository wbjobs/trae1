-- 分片规则表
CREATE TABLE IF NOT EXISTS t_shard_rule (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    logic_table VARCHAR(128) NOT NULL UNIQUE COMMENT '逻辑表名',
    sharding_column VARCHAR(64) NOT NULL COMMENT '分片列',
    algorithm VARCHAR(32) NOT NULL DEFAULT 'mod-long' COMMENT '分片算法',
    shard_count INT NOT NULL DEFAULT 2 COMMENT '分片数',
    primary_key VARCHAR(64) NOT NULL DEFAULT 'id' COMMENT '主键列',
    shard_nodes VARCHAR(512) COMMENT '分片节点列表,逗号分隔',
    status TINYINT DEFAULT 1 COMMENT '状态 1启用 0禁用',
    remark VARCHAR(512) COMMENT '备注',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='分片规则配置';

-- 同步任务表
CREATE TABLE IF NOT EXISTS t_sync_task (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    task_no VARCHAR(64) NOT NULL UNIQUE,
    task_name VARCHAR(128),
    sync_type VARCHAR(32) NOT NULL COMMENT 'FULL/INCREMENTAL/BINLOG',
    logic_table VARCHAR(128) NOT NULL,
    source_ds VARCHAR(128),
    target_ds VARCHAR(128),
    status VARCHAR(32) NOT NULL COMMENT 'PENDING/RUNNING/SUCCESS/PARTIAL/FAILED/CANCELED',
    trigger_mode VARCHAR(32) DEFAULT 'MANUAL',
    total_count BIGINT DEFAULT 0,
    success_count BIGINT DEFAULT 0,
    fail_count BIGINT DEFAULT 0,
    start_time DATETIME,
    end_time DATETIME,
    error_msg TEXT,
    params TEXT,
    retry_of BIGINT COMMENT '原始任务ID, 用于重试追踪',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_logic_table (logic_table),
    KEY idx_status (status),
    KEY idx_create_time (create_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='数据同步任务';

-- binlog 位点表
CREATE TABLE IF NOT EXISTS t_binlog_position (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    logic_table VARCHAR(128) NOT NULL UNIQUE,
    binlog_file VARCHAR(128),
    binlog_position BIGINT,
    gtid VARCHAR(128),
    server_id VARCHAR(64),
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Binlog 消费位点';

-- 增量事件表
CREATE TABLE IF NOT EXISTS t_incremental_event (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    event_id VARCHAR(64) NOT NULL UNIQUE,
    logic_table VARCHAR(128) NOT NULL,
    action VARCHAR(16) NOT NULL COMMENT 'INSERT/UPDATE/DELETE',
    pk_value VARCHAR(128),
    before_data TEXT,
    after_data TEXT,
    binlog_file VARCHAR(128),
    binlog_position BIGINT,
    status TINYINT DEFAULT 0 COMMENT '0待处理 1成功 2失败',
    retry_count INT DEFAULT 0,
    error_msg VARCHAR(1024),
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_logic_status (logic_table, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='增量同步事件';

-- 校验任务表
CREATE TABLE IF NOT EXISTS t_check_task (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    task_no VARCHAR(64) NOT NULL UNIQUE,
    logic_table VARCHAR(128) NOT NULL,
    check_type VARCHAR(32) NOT NULL DEFAULT 'ALL' COMMENT 'COUNT/CRC/ROW/ALL',
    status VARCHAR(32) NOT NULL,
    total_count BIGINT DEFAULT 0,
    diff_count BIGINT DEFAULT 0,
    start_time DATETIME,
    end_time DATETIME,
    error_msg TEXT,
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_logic_table (logic_table)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='数据校验任务';

-- 数据差异表
CREATE TABLE IF NOT EXISTS t_check_diff (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    task_id BIGINT NOT NULL,
    logic_table VARCHAR(128) NOT NULL,
    pk_value VARCHAR(128),
    diff_type VARCHAR(32) NOT NULL,
    shard_a VARCHAR(64),
    shard_b VARCHAR(64),
    source_data TEXT,
    target_data TEXT,
    fix_status TINYINT DEFAULT 0 COMMENT '0待修复 1成功 2失败',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    KEY idx_task_id (task_id),
    KEY idx_logic_fix (logic_table, fix_status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='数据差异记录';

-- 修复任务表
CREATE TABLE IF NOT EXISTS t_fix_task (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    task_no VARCHAR(64) NOT NULL UNIQUE,
    logic_table VARCHAR(128) NOT NULL,
    check_task_id BIGINT,
    status VARCHAR(32) NOT NULL,
    total_count BIGINT DEFAULT 0,
    fixed_count BIGINT DEFAULT 0,
    fail_count BIGINT DEFAULT 0,
    start_time DATETIME,
    end_time DATETIME,
    error_msg TEXT,
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_logic_table (logic_table)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='异常修复任务';
