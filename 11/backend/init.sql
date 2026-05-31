-- 数据库初始化脚本（如已配置 synchronize=true 可直接使用无需手动执行）
CREATE DATABASE IF NOT EXISTS api_doc DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
USE api_doc;

-- 默认管理员（如已通过 NestJS 自动初始化则可跳过）
-- 默认账号：admin / admin123
