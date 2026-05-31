-- 安全文件分享系统数据库初始化脚本

CREATE DATABASE IF NOT EXISTS secure_share 
CHARACTER SET utf8mb4 
COLLATE utf8mb4_unicode_ci;

USE secure_share;

-- 文件表
CREATE TABLE IF NOT EXISTS files (
  id VARCHAR(36) PRIMARY KEY,
  original_name VARCHAR(255) NOT NULL COMMENT '原始文件名',
  stored_name VARCHAR(255) NOT NULL COMMENT '存储文件名',
  file_path VARCHAR(500) NOT NULL COMMENT '文件存储路径',
  file_size BIGINT NOT NULL DEFAULT 0 COMMENT '文件大小(字节)',
  mime_type VARCHAR(100) COMMENT 'MIME类型',
  encryption_key VARCHAR(500) NOT NULL COMMENT 'AES加密密钥',
  encryption_iv VARCHAR(100) NOT NULL COMMENT 'AES加密IV',
  algorithm VARCHAR(50) NOT NULL DEFAULT 'aes-256-cbc' COMMENT '加密算法',
  owner_id VARCHAR(100) NOT NULL COMMENT '所有者ID',
  owner_name VARCHAR(100) COMMENT '所有者名称',
  status ENUM('active', 'expired', 'deleted') DEFAULT 'active' COMMENT '文件状态',
  expire_at DATETIME COMMENT '过期时间',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  INDEX idx_owner_id (owner_id),
  INDEX idx_status (status),
  INDEX idx_expire_at (expire_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='文件表';

-- 分享记录表
CREATE TABLE IF NOT EXISTS shares (
  id VARCHAR(36) PRIMARY KEY,
  file_id VARCHAR(36) NOT NULL COMMENT '文件ID',
  share_code VARCHAR(20) NOT NULL UNIQUE COMMENT '分享码',
  share_password VARCHAR(100) COMMENT '访问密码',
  permission_level ENUM('read', 'download', 'admin') DEFAULT 'read' COMMENT '权限级别',
  access_limit INT COMMENT '访问次数限制',
  access_count INT DEFAULT 0 COMMENT '已访问次数',
  allowed_users JSON COMMENT '允许访问的用户列表',
  expire_at DATETIME COMMENT '分享过期时间',
  status ENUM('active', 'expired', 'revoked') DEFAULT 'active' COMMENT '分享状态',
  created_by VARCHAR(100) NOT NULL COMMENT '创建者ID',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  INDEX idx_file_id (file_id),
  INDEX idx_share_code (share_code),
  INDEX idx_status (status),
  INDEX idx_expire_at (expire_at),
  FOREIGN KEY (file_id) REFERENCES files(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='分享记录表';
