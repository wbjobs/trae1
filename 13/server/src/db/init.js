const { pool } = require('./pool');

const CREATE_TABLES_SQL = `
CREATE TABLE IF NOT EXISTS mask_rules (
  id INT AUTO_INCREMENT PRIMARY KEY,
  name VARCHAR(128) NOT NULL,
  description VARCHAR(512) DEFAULT '',
  rule_type ENUM('regex','keyword','fixed') NOT NULL DEFAULT 'regex',
  pattern VARCHAR(1024) NOT NULL,
  replacement VARCHAR(256) NOT NULL DEFAULT '****',
  case_sensitive TINYINT(1) DEFAULT 0,
  enabled TINYINT(1) DEFAULT 1,
  priority INT DEFAULT 0,
  scope VARCHAR(64) DEFAULT 'all',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS audit_logs (
  id INT AUTO_INCREMENT PRIMARY KEY,
  file_name VARCHAR(256) NOT NULL,
  log_format VARCHAR(32) NOT NULL DEFAULT 'plain',
  total_lines INT DEFAULT 0,
  masked_lines INT DEFAULT 0,
  violation_lines INT DEFAULT 0,
  blocked TINYINT(1) DEFAULT 0,
  status ENUM('pending','passed','blocked','failed') DEFAULT 'pending',
  rule_snapshot JSON,
  original_path VARCHAR(512),
  masked_path VARCHAR(512),
  operator VARCHAR(64) DEFAULT 'system',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  INDEX idx_created_at (created_at),
  INDEX idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS violation_details (
  id INT AUTO_INCREMENT PRIMARY KEY,
  audit_id INT NOT NULL,
  line_number INT NOT NULL,
  rule_id INT,
  rule_name VARCHAR(128),
  matched_text VARCHAR(512),
  masked_text VARCHAR(512),
  severity ENUM('low','medium','high') DEFAULT 'medium',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_audit_id (audit_id),
  FOREIGN KEY (audit_id) REFERENCES audit_logs(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
`;

const DEFAULT_RULES = [
  {
    name: '手机号',
    description: '中国大陆手机号',
    rule_type: 'regex',
    pattern: '1[3-9]\\d{9}',
    replacement: '1*******$2',
    case_sensitive: 0,
    enabled: 1,
    priority: 100,
    scope: 'all'
  },
  {
    name: '身份证号',
    description: '18位身份证号码',
    rule_type: 'regex',
    pattern: '[1-9]\\d{5}(19|20)\\d{2}(0[1-9]|1[0-2])(0[1-9]|[12]\\d|3[01])\\d{3}[\\dXx]',
    replacement: '******************',
    case_sensitive: 0,
    enabled: 1,
    priority: 90,
    scope: 'all'
  },
  {
    name: '邮箱',
    description: '电子邮箱地址',
    rule_type: 'regex',
    pattern: '[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}',
    replacement: '***@***.***',
    case_sensitive: 0,
    enabled: 1,
    priority: 80,
    scope: 'all'
  },
  {
    name: '银行卡号',
    description: '银行卡号（16-19位）',
    rule_type: 'regex',
    pattern: '\\b\\d{16,19}\\b',
    replacement: '**** **** **** ****',
    case_sensitive: 0,
    enabled: 1,
    priority: 85,
    scope: 'all'
  },
  {
    name: 'IP地址',
    description: 'IPv4地址',
    rule_type: 'regex',
    pattern: '\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b',
    replacement: '***.***.***.***',
    case_sensitive: 0,
    enabled: 0,
    priority: 50,
    scope: 'all'
  },
  {
    name: '密码关键字',
    description: 'password/passwd/secret等关键字后的敏感值',
    rule_type: 'regex',
    pattern: '(password|passwd|secret|token|apikey|api_key)\\s*[=:]\\s*\\S+',
    replacement: '$1=****',
    case_sensitive: 0,
    enabled: 1,
    priority: 95,
    scope: 'all'
  }
];

async function init() {
  try {
    await pool.execute('CREATE DATABASE IF NOT EXISTS log_mask DEFAULT CHARACTER SET utf8mb4');
    console.log('[DB] 数据库 log_mask 就绪');
    const statements = CREATE_TABLES_SQL.split(';').map(s => s.trim()).filter(Boolean);
    for (const sql of statements) {
      await pool.execute(sql);
    }
    console.log('[DB] 数据表创建完成');

    const [existing] = await pool.execute('SELECT COUNT(*) AS cnt FROM mask_rules');
    if (existing[0].cnt === 0) {
      const sql = `INSERT INTO mask_rules (name,description,rule_type,pattern,replacement,case_sensitive,enabled,priority,scope)
                   VALUES (?,?,?,?,?,?,?,?,?)`;
      for (const r of DEFAULT_RULES) {
        await pool.execute(sql, [
          r.name, r.description, r.rule_type, r.pattern, r.replacement,
          r.case_sensitive, r.enabled, r.priority, r.scope
        ]);
      }
      console.log('[DB] 默认规则初始化完成');
    }
    process.exit(0);
  } catch (err) {
    console.error('[DB] 初始化失败:', err.message);
    process.exit(1);
  }
}

init();
