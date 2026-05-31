const { query, pool } = require('../db/pool');

const RULE_FIELDS = ['id', 'name', 'description', 'rule_type', 'pattern', 'replacement',
  'case_sensitive', 'enabled', 'priority', 'scope', 'created_at', 'updated_at'];

function validateRule(data, isUpdate = false) {
  const errors = [];
  if (!isUpdate && !data.name) errors.push('name 不能为空');
  if (data.name && data.name.length > 128) errors.push('name 不能超过128字符');
  if (data.description && data.description.length > 512) errors.push('description 不能超过512字符');
  if (!isUpdate && !data.pattern) errors.push('pattern 不能为空');
  if (data.pattern && data.pattern.length > 1024) errors.push('pattern 不能超过1024字符');
  if (data.rule_type && !['regex', 'keyword', 'fixed'].includes(data.rule_type)) {
    errors.push('rule_type 必须是 regex/keyword/fixed');
  }
  if (data.rule_type === 'regex') {
    try {
      new RegExp(data.pattern);
    } catch (e) {
      errors.push('pattern 不是合法的正则表达式: ' + e.message);
    }
  }
  if (data.replacement && data.replacement.length > 256) errors.push('replacement 不能超过256字符');
  if (data.priority !== undefined && (data.priority < 0 || data.priority > 999)) {
    errors.push('priority 必须在 0-999 之间');
  }
  return errors;
}

async function listRules(req, res) {
  try {
    const { enabled, keyword, page = 1, pageSize = 50 } = req.query;
    let where = '1=1';
    const params = [];
    if (enabled !== undefined) { where += ' AND enabled = ?'; params.push(enabled === '1' || enabled === 'true' ? 1 : 0); }
    if (keyword) { where += ' AND (name LIKE ? OR description LIKE ?)'; const k = `%${keyword}%`; params.push(k, k); }
    const [totalRow] = await pool.execute('SELECT COUNT(*) AS cnt FROM mask_rules WHERE ' + where, params);
    const total = totalRow[0].cnt;
    const offset = (page - 1) * pageSize;
    const sql = `SELECT ${RULE_FIELDS.join(',')} FROM mask_rules WHERE ${where}
                 ORDER BY priority DESC, id DESC LIMIT ? OFFSET ?`;
    const [list] = await pool.execute(sql, [...params, Number(pageSize), offset]);
    res.json({ code: 0, data: { list, total, page: Number(page), pageSize: Number(pageSize) } });
  } catch (err) {
    console.error('[Rule] list error:', err);
    res.status(500).json({ code: 500, message: '查询失败: ' + err.message });
  }
}

async function getRule(req, res) {
  try {
    const id = Number(req.params.id);
    if (!id) return res.status(400).json({ code: 400, message: 'id 无效' });
    const [rows] = await pool.execute('SELECT * FROM mask_rules WHERE id = ?', [id]);
    if (!rows.length) return res.status(404).json({ code: 404, message: '规则不存在' });
    res.json({ code: 0, data: rows[0] });
  } catch (err) {
    res.status(500).json({ code: 500, message: err.message });
  }
}

async function createRule(req, res) {
  try {
    const data = req.body || {};
    const errors = validateRule(data);
    if (errors.length) return res.status(400).json({ code: 400, message: errors.join('; ') });
    const sql = `INSERT INTO mask_rules (name,description,rule_type,pattern,replacement,case_sensitive,enabled,priority,scope)
                 VALUES (?,?,?,?,?,?,?,?,?)`;
    const [result] = await pool.execute(sql, [
      data.name, data.description || '', data.rule_type || 'regex',
      data.pattern, data.replacement || '****',
      data.case_sensitive ? 1 : 0, data.enabled !== undefined ? (data.enabled ? 1 : 0) : 1,
      data.priority || 0, data.scope || 'all'
    ]);
    res.json({ code: 0, data: { id: result.insertId } });
  } catch (err) {
    console.error('[Rule] create error:', err);
    res.status(500).json({ code: 500, message: '创建失败: ' + err.message });
  }
}

async function updateRule(req, res) {
  try {
    const id = Number(req.params.id);
    if (!id) return res.status(400).json({ code: 400, message: 'id 无效' });
    const data = req.body || {};
    const errors = validateRule(data, true);
    if (errors.length) return res.status(400).json({ code: 400, message: errors.join('; ') });
    const sets = [];
    const params = [];
    const fieldMap = {
      name: 'name', description: 'description', rule_type: 'rule_type',
      pattern: 'pattern', replacement: 'replacement',
      case_sensitive: 'case_sensitive', enabled: 'enabled',
      priority: 'priority', scope: 'scope'
    };
    for (const key of Object.keys(fieldMap)) {
      if (data[key] !== undefined) {
        sets.push(`${fieldMap[key]} = ?`);
        let v = data[key];
        if (key === 'case_sensitive' || key === 'enabled') v = v ? 1 : 0;
        params.push(v);
      }
    }
    if (sets.length === 0) return res.status(400).json({ code: 400, message: '没有可更新字段' });
    params.push(id);
    const [result] = await pool.execute(`UPDATE mask_rules SET ${sets.join(',')} WHERE id = ?`, params);
    if (result.affectedRows === 0) return res.status(404).json({ code: 404, message: '规则不存在' });
    res.json({ code: 0, data: { id } });
  } catch (err) {
    console.error('[Rule] update error:', err);
    res.status(500).json({ code: 500, message: '更新失败: ' + err.message });
  }
}

async function deleteRule(req, res) {
  try {
    const id = Number(req.params.id);
    if (!id) return res.status(400).json({ code: 400, message: 'id 无效' });
    const [result] = await pool.execute('DELETE FROM mask_rules WHERE id = ?', [id]);
    if (result.affectedRows === 0) return res.status(404).json({ code: 404, message: '规则不存在' });
    res.json({ code: 0, data: { id } });
  } catch (err) {
    res.status(500).json({ code: 500, message: '删除失败: ' + err.message });
  }
}

async function importRules(req, res) {
  try {
    const data = req.body || {};
    const rules = Array.isArray(data.rules) ? data.rules : (Array.isArray(data) ? data : null);
    if (!rules || rules.length === 0) return res.status(400).json({ code: 400, message: '规则数据不能为空' });
    if (rules.length > 500) return res.status(400).json({ code: 400, message: '单次最多导入500条规则' });
    const overwrite = data.overwrite === true;
    const skipOnError = data.skipOnError !== false;
    if (overwrite) {
      await pool.execute('DELETE FROM mask_rules');
    }
    const sql = `INSERT INTO mask_rules (name,description,rule_type,pattern,replacement,case_sensitive,enabled,priority,scope)
                 VALUES (?,?,?,?,?,?,?,?,?)`;
    let success = 0;
    const failed = [];
    for (let i = 0; i < rules.length; i++) {
      const r = rules[i];
      const errors = validateRule(r);
      if (errors.length) {
        failed.push({ index: i, name: r.name || '(unnamed)', errors });
        if (!skipOnError) {
          return res.status(400).json({ code: 400, message: `第${i + 1}条规则校验失败: ${errors.join('; ')}`, success, failed });
        }
        continue;
      }
      try {
        await pool.execute(sql, [
          r.name, r.description || '', r.rule_type || 'regex',
          r.pattern, r.replacement || '****',
          r.case_sensitive ? 1 : 0, r.enabled !== false ? 1 : 0,
          r.priority || 0, r.scope || 'all'
        ]);
        success++;
      } catch (e) {
        failed.push({ index: i, name: r.name, errors: [e.message] });
        if (!skipOnError) {
          return res.status(500).json({ code: 500, message: `第${i + 1}条规则导入失败: ${e.message}`, success, failed });
        }
      }
    }
    res.json({ code: 0, data: { success, failed: failed.length, failedDetails: failed.slice(0, 20) } });
  } catch (err) {
    console.error('[Rule] import error:', err);
    res.status(500).json({ code: 500, message: '导入失败: ' + err.message });
  }
}

async function exportRules(req, res) {
  try {
    const [rows] = await pool.execute('SELECT * FROM mask_rules ORDER BY priority DESC, id ASC');
    const exported = rows.map(r => ({
      name: r.name,
      description: r.description,
      rule_type: r.rule_type,
      pattern: r.pattern,
      replacement: r.replacement,
      case_sensitive: !!r.case_sensitive,
      enabled: !!r.enabled,
      priority: r.priority,
      scope: r.scope
    }));
    res.json({ code: 0, data: { count: exported.length, rules: exported } });
  } catch (err) {
    res.status(500).json({ code: 500, message: err.message });
  }
}

async function getTemplateSamples(req, res) {
  const samples = [
    {
      category: '个人信息',
      rules: [
        { name: '手机号', description: '中国大陆手机号', rule_type: 'regex', pattern: '1[3-9]\\d{9}', replacement: '1*******$2', priority: 100 },
        { name: '身份证号', description: '18位身份证', rule_type: 'regex', pattern: '[1-9]\\d{5}(19|20)\\d{2}(0[1-9]|1[0-2])(0[1-9]|[12]\\d|3[01])\\d{3}[\\dXx]', replacement: '******************', priority: 90 },
        { name: '姓名', description: '中文姓名2-4字', rule_type: 'regex', pattern: '[\\u4e00-\\u9fa5]{2,4}', replacement: '**', priority: 70 },
        { name: '地址', description: '省市地址信息', rule_type: 'regex', pattern: '[\\u4e00-\\u9fa5]{2,}(省|市|自治区|特别行政区)', replacement: '***', priority: 60 }
      ]
    },
    {
      category: '金融数据',
      rules: [
        { name: '银行卡号', description: '银行卡号16-19位', rule_type: 'regex', pattern: '\\b\\d{16,19}\\b', replacement: '**** **** **** ****', priority: 85 },
        { name: '信用卡CVV', description: '信用卡CVV码', rule_type: 'regex', pattern: '\\b\\d{3,4}\\b(?=.*cvv)', replacement: '***', priority: 95, case_sensitive: false },
        { name: '支付宝账号', description: '支付宝账号', rule_type: 'regex', pattern: '[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}', replacement: '***@***.***', priority: 80 }
      ]
    },
    {
      category: '系统安全',
      rules: [
        { name: '密码关键字', description: 'password/secret等', rule_type: 'regex', pattern: '(password|passwd|secret|token|apikey|api_key)\\s*[=:]\\s*\\S+', replacement: '$1=****', priority: 95, case_sensitive: false },
        { name: 'IP地址', description: 'IPv4地址', rule_type: 'regex', pattern: '\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b', replacement: '***.***.***.***', priority: 50, enabled: false },
        { name: 'JWT Token', description: 'JWT令牌', rule_type: 'regex', pattern: 'eyJ[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+', replacement: '***.***.***', priority: 100 }
      ]
    },
    {
      category: '网络服务',
      rules: [
        { name: '邮箱', description: '电子邮箱', rule_type: 'regex', pattern: '[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}', replacement: '***@***.***', priority: 80 },
        { name: 'URL域名', description: '敏感URL', rule_type: 'regex', pattern: 'https?://[\\w.-]+(admin|login|api\\.key|secret)[\\w./?-]*', replacement: '***', priority: 65 },
        { name: '内网IP', description: '内网私有地址', rule_type: 'regex', pattern: '\\b(10\\.\\d{1,3}|172\\.(1[6-9]|2\\d|3[01])|192\\.168)(\\.\\d{1,3}){2}\\b', replacement: '***.***.***.***', priority: 55 }
      ]
    }
  ];
  res.json({ code: 0, data: samples });
}

async function toggleRule(req, res) {
  try {
    const id = Number(req.params.id);
    const enabled = req.body && req.body.enabled;
    if (!id) return res.status(400).json({ code: 400, message: 'id 无效' });
    const [result] = await pool.execute('UPDATE mask_rules SET enabled = ? WHERE id = ?',
      [enabled ? 1 : 0, id]);
    if (result.affectedRows === 0) return res.status(404).json({ code: 404, message: '规则不存在' });
    res.json({ code: 0, data: { id } });
  } catch (err) {
    res.status(500).json({ code: 500, message: err.message });
  }
}

async function batchToggle(req, res) {
  try {
    const { ids, enabled } = req.body || {};
    if (!Array.isArray(ids) || ids.length === 0) return res.status(400).json({ code: 400, message: 'ids 不能为空数组' });
    const placeholders = ids.map(() => '?').join(',');
    await pool.execute(`UPDATE mask_rules SET enabled = ? WHERE id IN (${placeholders})`,
      [enabled ? 1 : 0, ...ids]);
    res.json({ code: 0, data: { count: ids.length } });
  } catch (err) {
    res.status(500).json({ code: 500, message: err.message });
  }
}

async function batchDelete(req, res) {
  try {
    const { ids } = req.body || {};
    if (!Array.isArray(ids) || ids.length === 0) return res.status(400).json({ code: 400, message: 'ids 不能为空数组' });
    const placeholders = ids.map(() => '?').join(',');
    const [result] = await pool.execute(`DELETE FROM mask_rules WHERE id IN (${placeholders})`, ids);
    res.json({ code: 0, data: { count: result.affectedRows } });
  } catch (err) {
    res.status(500).json({ code: 500, message: err.message });
  }
}

module.exports = {
  listRules, getRule, createRule, updateRule, deleteRule, toggleRule,
  importRules, exportRules, getTemplateSamples, batchToggle, batchDelete
};
