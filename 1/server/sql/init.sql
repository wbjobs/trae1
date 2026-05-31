-- 微前端权限管理系统数据库初始化脚本

CREATE DATABASE IF NOT EXISTS `micro_frontend` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE `micro_frontend`;

-- 用户表
CREATE TABLE IF NOT EXISTS `sys_users` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `username` VARCHAR(50) NOT NULL UNIQUE COMMENT '用户名',
  `password` VARCHAR(255) NOT NULL COMMENT '密码',
  `email` VARCHAR(100) NULL COMMENT '邮箱',
  `phone` VARCHAR(20) NULL COMMENT '手机号',
  `avatar` VARCHAR(255) NULL COMMENT '头像',
  `status` TINYINT DEFAULT 1 COMMENT '状态: 1-启用, 0-禁用',
  `last_login_at` DATETIME NULL COMMENT '最后登录时间',
  `last_login_ip` VARCHAR(50) NULL COMMENT '最后登录IP',
  `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  INDEX `idx_username` (`username`),
  INDEX `idx_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户表';

-- 角色表
CREATE TABLE IF NOT EXISTS `sys_roles` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `name` VARCHAR(50) NOT NULL COMMENT '角色名称',
  `code` VARCHAR(50) NOT NULL UNIQUE COMMENT '角色编码',
  `level` TINYINT DEFAULT 3 COMMENT '角色等级: 1-5, 1最高',
  `description` VARCHAR(255) NULL COMMENT '角色描述',
  `status` TINYINT DEFAULT 1 COMMENT '状态: 1-启用, 0-禁用',
  `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  INDEX `idx_code` (`code`),
  INDEX `idx_level` (`level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='角色表';

-- 权限表
CREATE TABLE IF NOT EXISTS `sys_permissions` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `parent_id` BIGINT UNSIGNED NULL COMMENT '父级权限ID',
  `name` VARCHAR(50) NOT NULL COMMENT '权限名称',
  `code` VARCHAR(100) NOT NULL UNIQUE COMMENT '权限编码',
  `type` VARCHAR(20) NOT NULL COMMENT '权限类型: menu, button, api',
  `app` VARCHAR(50) NULL COMMENT '所属子应用',
  `path` VARCHAR(255) NULL COMMENT '路由路径',
  `method` VARCHAR(20) NULL COMMENT '请求方法',
  `icon` VARCHAR(50) NULL COMMENT '图标',
  `sort` INTEGER DEFAULT 0 COMMENT '排序',
  `description` VARCHAR(255) NULL COMMENT '权限描述',
  `status` TINYINT DEFAULT 1 COMMENT '状态: 1-启用, 0-禁用',
  `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  INDEX `idx_code` (`code`),
  INDEX `idx_parent_id` (`parent_id`),
  INDEX `idx_app` (`app`),
  INDEX `idx_type` (`type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='权限表';

-- 用户角色关联表
CREATE TABLE IF NOT EXISTS `sys_user_roles` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `user_id` BIGINT UNSIGNED NOT NULL COMMENT '用户ID',
  `role_id` BIGINT UNSIGNED NOT NULL COMMENT '角色ID',
  `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  UNIQUE KEY `uk_user_role` (`user_id`, `role_id`),
  INDEX `idx_user_id` (`user_id`),
  INDEX `idx_role_id` (`role_id`),
  FOREIGN KEY (`user_id`) REFERENCES `sys_users` (`id`) ON DELETE CASCADE,
  FOREIGN KEY (`role_id`) REFERENCES `sys_roles` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户角色关联表';

-- 角色权限关联表
CREATE TABLE IF NOT EXISTS `sys_role_permissions` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `role_id` BIGINT UNSIGNED NOT NULL COMMENT '角色ID',
  `permission_id` BIGINT UNSIGNED NOT NULL COMMENT '权限ID',
  `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  UNIQUE KEY `uk_role_permission` (`role_id`, `permission_id`),
  INDEX `idx_role_id` (`role_id`),
  INDEX `idx_permission_id` (`permission_id`),
  FOREIGN KEY (`role_id`) REFERENCES `sys_roles` (`id`) ON DELETE CASCADE,
  FOREIGN KEY (`permission_id`) REFERENCES `sys_permissions` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='角色权限关联表';

-- 操作日志表
CREATE TABLE IF NOT EXISTS `sys_operation_logs` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `user_id` BIGINT UNSIGNED NULL COMMENT '用户ID',
  `username` VARCHAR(50) NULL COMMENT '用户名',
  `module` VARCHAR(50) NULL COMMENT '模块',
  `action` VARCHAR(50) NULL COMMENT '操作类型',
  `description` VARCHAR(500) NULL COMMENT '操作描述',
  `method` VARCHAR(20) NULL COMMENT '请求方法',
  `request_url` VARCHAR(500) NULL COMMENT '请求URL',
  `request_params` TEXT NULL COMMENT '请求参数',
  `response_data` TEXT NULL COMMENT '响应数据',
  `ip` VARCHAR(50) NULL COMMENT 'IP地址',
  `user_agent` VARCHAR(500) NULL COMMENT '用户代理',
  `duration` INTEGER NULL COMMENT '耗时(毫秒)',
  `status` VARCHAR(20) DEFAULT 'success' COMMENT '状态: success, error',
  `error_message` TEXT NULL COMMENT '错误信息',
  `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  INDEX `idx_user_id` (`user_id`),
  INDEX `idx_module` (`module`),
  INDEX `idx_action` (`action`),
  INDEX `idx_status` (`status`),
  INDEX `idx_created_at` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='操作日志表';

-- 审计日志表
CREATE TABLE IF NOT EXISTS `sys_audit_logs` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `operation_type` VARCHAR(20) NOT NULL COMMENT '操作类型: create, update, delete, query',
  `table_name` VARCHAR(100) NOT NULL COMMENT '数据表名',
  `record_id` VARCHAR(100) NULL COMMENT '记录ID',
  `old_value` TEXT NULL COMMENT '修改前数据',
  `new_value` TEXT NULL COMMENT '修改后数据',
  `operator` VARCHAR(50) NULL COMMENT '操作人',
  `operator_id` BIGINT UNSIGNED NULL COMMENT '操作人ID',
  `ip` VARCHAR(50) NULL COMMENT 'IP地址',
  `operation_time` DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '操作时间',
  `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  INDEX `idx_operation_type` (`operation_type`),
  INDEX `idx_table_name` (`table_name`),
  INDEX `idx_operator_id` (`operator_id`),
  INDEX `idx_operation_time` (`operation_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='审计日志表';

-- 系统配置表
CREATE TABLE IF NOT EXISTS `sys_system_config` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `config_key` VARCHAR(100) NOT NULL UNIQUE COMMENT '配置键',
  `config_value` TEXT NULL COMMENT '配置值',
  `config_type` VARCHAR(20) DEFAULT 'string' COMMENT '值类型: string, number, boolean, json',
  `description` VARCHAR(255) NULL COMMENT '配置描述',
  `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  INDEX `idx_config_key` (`config_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='系统配置表';

-- 初始数据

-- 插入默认角色
INSERT INTO `sys_roles` (`name`, `code`, `level`, `description`, `status`) VALUES
('超级管理员', 'super_admin', 1, '系统超级管理员，拥有所有权限', 1),
('系统管理员', 'system_admin', 2, '系统管理员，拥有大部分系统管理权限', 1),
('数据审计员', 'data_auditor', 3, '数据审计员，负责数据审计相关操作', 1),
('普通用户', 'normal_user', 4, '普通用户，拥有基础查看权限', 1),
('访客', 'guest', 5, '访客，仅有查看权限', 1)
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);

-- 插入权限数据
INSERT INTO `sys_permissions` (`parent_id`, `name`, `code`, `type`, `app`, `path`, `method`, `icon`, `sort`, `description`, `status`) VALUES
(NULL, '仪表盘', 'dashboard', 'menu', NULL, '/dashboard', NULL, 'el-icon-s-home', 1, '仪表盘页面', 1),

(NULL, '系统配置', 'system-config', 'menu', 'system-config', '/system-config', NULL, 'el-icon-setting', 2, '系统配置子应用', 1),
(NULL, '系统配置-查看', 'config:view', 'api', 'system-config', '/api/system-config', 'GET', NULL, 21, '查看系统配置', 1),
(NULL, '系统配置-修改', 'config:update', 'api', 'system-config', '/api/system-config', 'POST', NULL, 22, '修改系统配置', 1),
(NULL, '系统配置-删除', 'config:delete', 'api', 'system-config', '/api/system-config/:key', 'DELETE', NULL, 23, '删除系统配置', 1),

(NULL, '数据审计', 'data-audit', 'menu', 'data-audit', '/data-audit', NULL, 'el-icon-document', 3, '数据审计子应用', 1),
(NULL, '数据审计-查看', 'audit:view', 'api', 'data-audit', '/api/logs/audit', 'GET', NULL, 31, '查看审计日志', 1),

(NULL, '操作日志', 'operation-log', 'menu', 'operation-log', '/operation-log', NULL, 'el-icon-tickets', 4, '操作日志子应用', 1),
(NULL, '操作日志-查看', 'log:view', 'api', 'operation-log', '/api/logs/operation', 'GET', NULL, 41, '查看操作日志', 1),
(NULL, '操作日志-删除', 'log:delete', 'api', 'operation-log', '/api/logs/cleanup', 'POST', NULL, 42, '清理操作日志', 1),

(NULL, '角色分级', 'role-management', 'menu', 'role-management', '/role-management', NULL, 'el-icon-s-custom', 5, '角色分级子应用', 1),
(NULL, '角色-列表', 'role:list', 'api', 'role-management', '/api/roles', 'GET', NULL, 51, '查看角色列表', 1),
(NULL, '角色-查看', 'role:view', 'api', 'role-management', '/api/roles/:id', 'GET', NULL, 52, '查看角色详情', 1),
(NULL, '角色-创建', 'role:create', 'api', 'role-management', '/api/roles', 'POST', NULL, 53, '创建角色', 1),
(NULL, '角色-更新', 'role:update', 'api', 'role-management', '/api/roles/:id', 'PUT', NULL, 54, '更新角色', 1),
(NULL, '角色-删除', 'role:delete', 'api', 'role-management', '/api/roles/:id', 'DELETE', NULL, 55, '删除角色', 1),
(NULL, '角色-分配权限', 'role:assign', 'api', 'role-management', '/api/roles/:id/permissions', 'POST', NULL, 56, '分配角色权限', 1),

(NULL, '用户管理', 'user', 'menu', NULL, '/users', NULL, 'el-icon-user', 6, '用户管理菜单', 1),
(NULL, '用户-列表', 'user:list', 'api', NULL, '/api/users', 'GET', NULL, 61, '查看用户列表', 1),
(NULL, '用户-查看', 'user:view', 'api', NULL, '/api/users/:id', 'GET', NULL, 62, '查看用户详情', 1),
(NULL, '用户-创建', 'user:create', 'api', NULL, '/api/users', 'POST', NULL, 63, '创建用户', 1),
(NULL, '用户-更新', 'user:update', 'api', NULL, '/api/users/:id', 'PUT', NULL, 64, '更新用户', 1),
(NULL, '用户-删除', 'user:delete', 'api', NULL, '/api/users/:id', 'DELETE', NULL, 65, '删除用户', 1),

(NULL, '权限管理', 'permission', 'menu', NULL, '/permissions', NULL, 'el-icon-key', 7, '权限管理菜单', 1),
(NULL, '权限-列表', 'permission:list', 'api', NULL, '/api/permissions/all', 'GET', NULL, 71, '查看权限列表', 1),
(NULL, '权限-创建', 'permission:create', 'api', NULL, '/api/permissions', 'POST', NULL, 72, '创建权限', 1),
(NULL, '权限-更新', 'permission:update', 'api', NULL, '/api/permissions/:id', 'PUT', NULL, 73, '更新权限', 1),
(NULL, '权限-删除', 'permission:delete', 'api', NULL, '/api/permissions/:id', 'DELETE', NULL, 74, '删除权限', 1)
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);

-- 为超级管理员分配所有权限
INSERT INTO `sys_role_permissions` (`role_id`, `permission_id`)
SELECT r.id, p.id FROM `sys_roles` r, `sys_permissions` p
WHERE r.code = 'super_admin'
ON DUPLICATE KEY UPDATE `role_id` = `role_id`;

-- 为系统管理员分配大部分权限（排除用户删除和权限管理）
INSERT INTO `sys_role_permissions` (`role_id`, `permission_id`)
SELECT r.id, p.id FROM `sys_roles` r, `sys_permissions` p
WHERE r.code = 'system_admin' AND p.code NOT IN ('user:delete', 'permission:create', 'permission:update', 'permission:delete')
ON DUPLICATE KEY UPDATE `role_id` = `role_id`;

-- 为数据审计员分配数据审计和日志查看权限
INSERT INTO `sys_role_permissions` (`role_id`, `permission_id`)
SELECT r.id, p.id FROM `sys_roles` r, `sys_permissions` p
WHERE r.code = 'data_auditor' AND p.code IN ('dashboard', 'data-audit', 'audit:view', 'operation-log', 'log:view')
ON DUPLICATE KEY UPDATE `role_id` = `role_id`;

-- 为普通用户分配基础查看权限
INSERT INTO `sys_role_permissions` (`role_id`, `permission_id`)
SELECT r.id, p.id FROM `sys_roles` r, `sys_permissions` p
WHERE r.code = 'normal_user' AND p.code IN ('dashboard')
ON DUPLICATE KEY UPDATE `role_id` = `role_id`;

-- 为访客分配查看权限
INSERT INTO `sys_role_permissions` (`role_id`, `permission_id`)
SELECT r.id, p.id FROM `sys_roles` r, `sys_permissions` p
WHERE r.code = 'guest' AND p.code IN ('dashboard')
ON DUPLICATE KEY UPDATE `role_id` = `role_id`;

-- 插入默认用户（密码: admin123）
INSERT INTO `sys_users` (`username`, `password`, `email`, `phone`, `status`) VALUES
('admin', '$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy', 'admin@example.com', '13800138000', 1),
('auditor', '$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy', 'auditor@example.com', '13800138001', 1),
('user', '$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy', 'user@example.com', '13800138002', 1)
ON DUPLICATE KEY UPDATE `username` = VALUES(`username`);

-- 为用户分配角色
INSERT INTO `sys_user_roles` (`user_id`, `role_id`)
SELECT u.id, r.id FROM `sys_users` u, `sys_roles` r
WHERE u.username = 'admin' AND r.code = 'super_admin'
ON DUPLICATE KEY UPDATE `user_id` = `user_id`;

INSERT INTO `sys_user_roles` (`user_id`, `role_id`)
SELECT u.id, r.id FROM `sys_users` u, `sys_roles` r
WHERE u.username = 'auditor' AND r.code = 'data_auditor'
ON DUPLICATE KEY UPDATE `user_id` = `user_id`;

INSERT INTO `sys_user_roles` (`user_id`, `role_id`)
SELECT u.id, r.id FROM `sys_users` u, `sys_roles` r
WHERE u.username = 'user' AND r.code = 'normal_user'
ON DUPLICATE KEY UPDATE `user_id` = `user_id`;

-- 插入默认系统配置
INSERT INTO `sys_system_config` (`config_key`, `config_value`, `config_type`, `description`) VALUES
('system.name', '微前端管理平台', 'string', '系统名称'),
('system.version', '1.0.0', 'string', '系统版本'),
('system.maintenance_mode', 'false', 'boolean', '维护模式'),
('security.min_password_length', '8', 'number', '密码最小长度'),
('security.password_expiry_days', '90', 'number', '密码有效期(天)'),
('security.login_fail_lock_count', '5', 'number', '登录失败锁定次数'),
('security.session_timeout', '30', 'number', 'Session超时时间(分钟)'),
('security.enable_2fa', 'false', 'boolean', '启用双因素认证'),
('notification.smtp_server', 'smtp.example.com', 'string', '邮件服务器'),
('notification.smtp_port', '465', 'number', '邮件端口'),
('notification.enable_email', 'false', 'boolean', '启用邮件通知'),
('notification.enable_webhook', 'false', 'boolean', '启用Webhook通知')
ON DUPLICATE KEY UPDATE `config_value` = VALUES(`config_value`);
