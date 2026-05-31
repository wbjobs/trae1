const fs = require('fs');
const path = require('path');
const readline = require('readline');
const { v4: uuidv4 } = require('uuid');
const dayjs = require('dayjs');
const { pool } = require('../db/pool');
const { readLines, countLines, detectFormat, parseLine } = require('../utils/logParser');
const { compileRules, maskLine, maskLineSimple } = require('../utils/maskEngine');

const UPLOAD_DIR = path.join(__dirname, '../../uploads');
const MASKED_DIR = path.join(__dirname, '../../masked');
if (!fs.existsSync(UPLOAD_DIR)) fs.mkdirSync(UPLOAD_DIR, { recursive: true });
if (!fs.existsSync(MASKED_DIR)) fs.mkdirSync(MASKED_DIR, { recursive: true });

const taskQueue = new Map();

async function getEnabledRules() {
  const [rows] = await pool.execute('SELECT * FROM mask_rules WHERE enabled = 1 ORDER BY priority DESC');
  return rows;
}

async function uploadLog(req, res) {
  try {
    if (!req.file) return res.status(400).json({ code: 400, message: '请上传日志文件' });
    const { format, operator = 'system' } = req.body || {};
    const fileId = uuidv4();
    const ext = path.extname(req.file.originalname);
    const fileName = `${fileId}${ext}`;
    const originalPath = path.join(UPLOAD_DIR, fileName);
    fs.renameSync(req.file.path, originalPath);
    const sample = await readLines(originalPath, { maxLines: 20 });
    const detected = format || detectFormat(sample);
    const totalLines = await countLines(originalPath);
    const [result] = await pool.execute(
      `INSERT INTO audit_logs (file_name,log_format,total_lines,status,original_path,operator)
       VALUES (?,?,?,'pending',?,?)`,
      [req.file.originalname, detected, totalLines, originalPath, operator]
    );
    res.json({
      code: 0,
      data: {
        auditId: result.insertId,
        fileName: req.file.originalname,
        format: detected,
        totalLines,
        fileId
      }
    });
  } catch (err) {
    console.error('[Log] upload error:', err);
    res.status(500).json({ code: 500, message: '上传失败: ' + err.message });
  }
}

async function previewLog(req, res) {
  try {
    const { auditId, format, page = 1, pageSize = 50 } = req.query;
    const aid = Number(auditId);
    if (!aid) return res.status(400).json({ code: 400, message: 'auditId 无效' });
    const [rows] = await pool.execute('SELECT * FROM audit_logs WHERE id = ?', [aid]);
    if (!rows.length) return res.status(404).json({ code: 404, message: '记录不存在' });
    const record = rows[0];
    const actualFormat = format || record.log_format;
    if (!record.original_path || !fs.existsSync(record.original_path)) {
      return res.status(404).json({ code: 404, message: '原始文件不存在' });
    }
    const lines = await readLinesPaged(record.original_path, page, pageSize);
    const parsed = lines.map((line, idx) => {
      const p = parseLine(line, actualFormat);
      return { lineNumber: (page - 1) * pageSize + idx + 1, raw: line, fields: p.fields };
    });
    res.json({ code: 0, data: { lines: parsed, total: record.total_lines, format: actualFormat, page: Number(page) } });
  } catch (err) {
    console.error('[Log] preview error:', err);
    res.status(500).json({ code: 500, message: err.message });
  }
}

function readLinesPaged(filePath, page, pageSize) {
  return new Promise((resolve, reject) => {
    const lines = [];
    const skip = (page - 1) * pageSize;
    let count = 0;
    const rl = readline.createInterface({
      input: fs.createReadStream(filePath, { highWaterMark: 128 * 1024 }),
      crlfDelay: Infinity
    });
    rl.on('line', (line) => {
      if (count >= skip && lines.length < pageSize) lines.push(line);
      count++;
      if (lines.length >= pageSize) rl.close();
    });
    rl.on('close', () => resolve(lines));
    rl.on('error', reject);
  });
}

async function compareLog(req, res) {
  try {
    const { auditId, format, page = 1, pageSize = 50 } = req.query;
    const aid = Number(auditId);
    if (!aid) return res.status(400).json({ code: 400, message: 'auditId 无效' });
    const [rows] = await pool.execute('SELECT * FROM audit_logs WHERE id = ?', [aid]);
    if (!rows.length) return res.status(404).json({ code: 404, message: '记录不存在' });
    const record = rows[0];
    const actualFormat = format || record.log_format;
    if (!record.original_path || !fs.existsSync(record.original_path)) {
      return res.status(404).json({ code: 404, message: '原始文件不存在' });
    }
    const rules = await getEnabledRules();
    const compiled = compileRules(rules);
    const origLines = await readLinesPaged(record.original_path, page, pageSize);
    const comparisons = origLines.map((line, idx) => {
      const lineNo = (page - 1) * pageSize + idx + 1;
      const result = maskLine(line, actualFormat, compiled, lineNo);
      return {
        lineNumber: lineNo,
        original: line,
        masked: result.masked,
        changed: result.changed,
        violations: result.violations
      };
    });
    res.json({
      code: 0,
      data: {
        comparisons,
        total: record.total_lines,
        format: actualFormat,
        changedCount: comparisons.filter(c => c.changed).length
      }
    });
  } catch (err) {
    console.error('[Log] compare error:', err);
    res.status(500).json({ code: 500, message: err.message });
  }
}

async function maskLog(req, res) {
  try {
    const { auditId, format, blockOnViolation = true, operator = 'system' } = req.body || {};
    const aid = Number(auditId);
    if (!aid) return res.status(400).json({ code: 400, message: 'auditId 无效' });
    const [rows] = await pool.execute('SELECT * FROM audit_logs WHERE id = ?', [aid]);
    if (!rows.length) return res.status(404).json({ code: 404, message: '记录不存在' });
    const record = rows[0];
    if (!record.original_path || !fs.existsSync(record.original_path)) {
      return res.status(404).json({ code: 404, message: '原始文件不存在' });
    }
    const actualFormat = format || record.log_format;
    const rules = await getEnabledRules();
    const compiled = compileRules(rules);
    const ruleSnapshot = JSON.stringify(rules);

    const fileId = uuidv4();
    const maskedPath = path.join(MASKED_DIR, `${fileId}.log`);

    await pool.execute(`UPDATE audit_logs SET status = 'processing', operator = ?, updated_at = NOW() WHERE id = ?`, [operator, aid]);

    const { totalLines, maskedCount, violations, sampleMasked } = await processFileChunked(
      record.original_path,
      maskedPath,
      actualFormat,
      compiled,
      aid
    );

    const hasViolation = violations.some(v => v.severity === 'high');
    const blocked = blockOnViolation && hasViolation;
    const status = blocked ? 'blocked' : 'passed';

    await pool.execute(
      `UPDATE audit_logs SET log_format=?, masked_lines=?, violation_lines=?,
       blocked=?, status=?, rule_snapshot=?, masked_path=?, operator=?, updated_at=NOW()
       WHERE id=?`,
      [actualFormat, maskedCount, violations.length,
        blocked ? 1 : 0, status, ruleSnapshot, blocked ? null : maskedPath, operator, aid]
    );

    if (violations.length > 0) {
      const batch = violations.slice(0, 2000);
      const placeholders = batch.map(() => '(?,?,?,?,?,?,?)').join(',');
      const vSql = `INSERT INTO violation_details (audit_id,line_number,rule_id,rule_name,matched_text,masked_text,severity)
                    VALUES ${placeholders}`;
      const params = [];
      for (const v of batch) {
        params.push(aid, v.line_number, v.rule_id, v.rule_name, v.matched_text, v.masked_text, v.severity);
      }
      await pool.execute(vSql, params);
    }

    if (blocked && fs.existsSync(maskedPath)) {
      fs.unlinkSync(maskedPath);
    }

    res.json({
      code: 0,
      data: {
        auditId: aid,
        status,
        blocked,
        totalLines,
        maskedLines: maskedCount,
        violationCount: violations.length,
        highViolations: violations.filter(v => v.severity === 'high').length,
        maskedPath: blocked ? null : maskedPath,
        sampleMasked
      }
    });
  } catch (err) {
    console.error('[Log] mask error:', err);
    await pool.execute(`UPDATE audit_logs SET status = 'failed', updated_at = NOW() WHERE id = ?`, [req.body?.auditId]);
    res.status(500).json({ code: 500, message: '脱敏失败: ' + err.message });
  }
}

async function startMaskTask(req, res) {
  try {
    const { auditId, format, blockOnViolation = true, operator = 'system' } = req.body || {};
    const aid = Number(auditId);
    if (!aid) return res.status(400).json({ code: 400, message: 'auditId 无效' });
    const taskId = uuidv4();
    taskQueue.set(taskId, { status: 'queued', progress: 0, auditId: aid, startTime: Date.now() });

    setImmediate(async () => {
      try {
        taskQueue.get(taskId).status = 'processing';
        const [rows] = await pool.execute('SELECT * FROM audit_logs WHERE id = ?', [aid]);
        if (!rows.length) throw new Error('记录不存在');
        const record = rows[0];
        if (!record.original_path || !fs.existsSync(record.original_path)) throw new Error('文件不存在');
        const actualFormat = format || record.log_format;
        const rules = await getEnabledRules();
        const compiled = compileRules(rules);
        const ruleSnapshot = JSON.stringify(rules);

        await pool.execute(`UPDATE audit_logs SET status = 'processing' WHERE id = ?`, [aid]);

        const fileId = uuidv4();
        const maskedPath = path.join(MASKED_DIR, `${fileId}.log`);

        const { totalLines, maskedCount, violations } = await processFileChunked(
          record.original_path, maskedPath, actualFormat, compiled, aid,
          (progress) => { const t = taskQueue.get(taskId); if (t) t.progress = progress; }
        );

        const hasViolation = violations.some(v => v.severity === 'high');
        const blocked = blockOnViolation && hasViolation;
        const status = blocked ? 'blocked' : 'passed';

        await pool.execute(
          `UPDATE audit_logs SET masked_lines=?, violation_lines=?, blocked=?, status=?, rule_snapshot=?, masked_path=? WHERE id=?`,
          [maskedCount, violations.length, blocked ? 1 : 0, status, ruleSnapshot, blocked ? null : maskedPath, aid]
        );

        if (violations.length > 0) {
          const batch = violations.slice(0, 2000);
          const placeholders = batch.map(() => '(?,?,?,?,?,?,?)').join(',');
          const vSql = `INSERT INTO violation_details (audit_id,line_number,rule_id,rule_name,matched_text,masked_text,severity) VALUES ${placeholders}`;
          const params = [];
          for (const v of batch) params.push(aid, v.line_number, v.rule_id, v.rule_name, v.matched_text, v.masked_text, v.severity);
          await pool.execute(vSql, params);
        }

        if (blocked && fs.existsSync(maskedPath)) fs.unlinkSync(maskedPath);
        const t = taskQueue.get(taskId);
        if (t) { t.status = 'done'; t.progress = 100; t.result = { status, blocked, totalLines, maskedCount, violationCount: violations.length }; }
      } catch (e) {
        console.error('[Task] error:', e);
        const t = taskQueue.get(taskId);
        if (t) { t.status = 'failed'; t.error = e.message; }
        await pool.execute(`UPDATE audit_logs SET status = 'failed' WHERE id = ?`, [aid]);
      }
    });

    res.json({ code: 0, data: { taskId } });
  } catch (err) {
    res.status(500).json({ code: 500, message: err.message });
  }
}

function getTaskStatus(req, res) {
  const taskId = req.params.taskId;
  const task = taskQueue.get(taskId);
  if (!task) return res.status(404).json({ code: 404, message: '任务不存在' });
  res.json({ code: 0, data: task });
}

function processFileChunked(inputPath, outputPath, format, compiledRules, auditId, onProgress) {
  return new Promise((resolve, reject) => {
    const CHUNK_SIZE = 1000;
    const stream = fs.createReadStream(inputPath, { highWaterMark: 256 * 1024 });
    const rl = readline.createInterface({ input: stream, crlfDelay: Infinity });
    const outStream = fs.createWriteStream(outputPath, { highWaterMark: 512 * 1024 });

    let lineNumber = 0;
    let totalLines = 0;
    let maskedCount = 0;
    const violations = [];
    const sampleMasked = [];
    let chunkBuffer = [];
    let chunkIndex = 0;
    let totalSize = 0;
    try { totalSize = fs.statSync(inputPath).size; } catch { totalSize = 1; }
    let bytesRead = 0;

    stream.on('data', (chunk) => { bytesRead += chunk.length; });

    rl.on('line', (line) => {
      lineNumber++;
      totalLines++;
      try {
        const result = maskLine(line, format, compiledRules, lineNumber);
        if (result.changed) {
          maskedCount++;
          for (const v of result.violations) {
            if (violations.length < 5000) violations.push(v);
          }
          if (sampleMasked.length < 20) sampleMasked.push(result.masked);
        }
        chunkBuffer.push(result.masked);
      } catch {
        chunkBuffer.push(line);
      }

      if (chunkBuffer.length >= CHUNK_SIZE) {
        outStream.write(chunkBuffer.join('\n') + '\n');
        chunkBuffer = [];
        chunkIndex++;
        if (onProgress) onProgress(Math.min(99, Math.floor((bytesRead / totalSize) * 100)));
      }
    });

    rl.on('close', () => {
      if (chunkBuffer.length > 0) {
        outStream.write(chunkBuffer.join('\n') + '\n');
        chunkBuffer = [];
      }
      outStream.end(() => {
        if (onProgress) onProgress(100);
        resolve({ totalLines, maskedCount, violations, sampleMasked });
      });
    });

    rl.on('error', reject);
    outStream.on('error', reject);
  });
}

async function listAuditLogs(req, res) {
  try {
    const { status, keyword, format, page = 1, pageSize = 20 } = req.query;
    let where = '1=1';
    const params = [];
    if (status) { where += ' AND status = ?'; params.push(status); }
    if (keyword) { where += ' AND file_name LIKE ?'; params.push(`%${keyword}%`); }
    if (format) { where += ' AND log_format = ?'; params.push(format); }
    const [totalRow] = await pool.execute('SELECT COUNT(*) AS cnt FROM audit_logs WHERE ' + where, params);
    const total = totalRow[0].cnt;
    const offset = (page - 1) * pageSize;
    const [list] = await pool.execute(
      `SELECT * FROM audit_logs WHERE ${where} ORDER BY id DESC LIMIT ? OFFSET ?`,
      [...params, Number(pageSize), offset]
    );
    res.json({ code: 0, data: { list, total, page: Number(page), pageSize: Number(pageSize) } });
  } catch (err) {
    res.status(500).json({ code: 500, message: err.message });
  }
}

async function getAuditDetail(req, res) {
  try {
    const id = Number(req.params.id);
    if (!id) return res.status(400).json({ code: 400, message: 'id 无效' });
    const [rows] = await pool.execute('SELECT * FROM audit_logs WHERE id = ?', [id]);
    if (!rows.length) return res.status(404).json({ code: 404, message: '记录不存在' });
    const [violations] = await pool.execute(
      'SELECT * FROM violation_details WHERE audit_id = ? ORDER BY line_number ASC LIMIT 500',
      [id]
    );
    res.json({ code: 0, data: { ...rows[0], violations } });
  } catch (err) {
    res.status(500).json({ code: 500, message: err.message });
  }
}

async function downloadMasked(req, res) {
  try {
    const id = Number(req.params.id);
    if (!id) return res.status(400).json({ code: 400, message: 'id 无效' });
    const [rows] = await pool.execute('SELECT * FROM audit_logs WHERE id = ?', [id]);
    if (!rows.length) return res.status(404).json({ code: 404, message: '记录不存在' });
    const record = rows[0];
    if (record.blocked) return res.status(403).json({ code: 403, message: '该日志已被拦截，禁止下载' });
    if (!record.masked_path || !fs.existsSync(record.masked_path)) {
      return res.status(404).json({ code: 404, message: '脱敏文件不存在' });
    }
    res.download(record.masked_path, `masked_${record.file_name}`);
  } catch (err) {
    res.status(500).json({ code: 500, message: err.message });
  }
}

async function testMask(req, res) {
  try {
    const { content, format = 'plain', ruleIds } = req.body || {};
    if (!content) return res.status(400).json({ code: 400, message: 'content 不能为空' });
    let rules;
    if (ruleIds && ruleIds.length > 0) {
      const placeholders = ruleIds.map(() => '?').join(',');
      const [rows] = await pool.execute(`SELECT * FROM mask_rules WHERE id IN (${placeholders}) AND enabled = 1`, ruleIds);
      rules = rows;
    } else {
      rules = await getEnabledRules();
    }
    const compiled = compileRules(rules);
    const lines = content.split('\n');
    const results = [];
    for (let i = 0; i < lines.length; i++) {
      const r = maskLine(lines[i], format, compiled, i + 1);
      results.push(r);
    }
    res.json({
      code: 0,
      data: {
        maskedContent: results.map(r => r.masked).join('\n'),
        comparisons: results.map(r => ({ original: r.original, masked: r.masked, changed: r.changed, violations: r.violations })),
        totalLines: lines.length,
        maskedLines: results.filter(r => r.changed).length,
        violations: results.flatMap(r => r.violations)
      }
    });
  } catch (err) {
    res.status(500).json({ code: 500, message: err.message });
  }
}

async function getStatistics(req, res) {
  try {
    const { days = 30, groupBy = 'day' } = req.query;
    const since = dayjs().subtract(Number(days), 'day').format('YYYY-MM-DD HH:mm:ss');

    const [byStatus] = await pool.execute(
      `SELECT status, COUNT(*) as count FROM audit_logs WHERE created_at >= ? GROUP BY status`,
      [since]
    );
    const [byFormat] = await pool.execute(
      `SELECT log_format as format, COUNT(*) as count, SUM(masked_lines) as masked, SUM(violation_lines) as violations FROM audit_logs WHERE created_at >= ? AND status IN ('passed','blocked') GROUP BY log_format`,
      [since]
    );
    const [topRules] = await pool.execute(
      `SELECT v.rule_id, v.rule_name, COUNT(*) as hit_count FROM violation_details v
       JOIN audit_logs a ON v.audit_id = a.id
       WHERE a.created_at >= ?
       GROUP BY v.rule_id, v.rule_name
       ORDER BY hit_count DESC LIMIT 15`,
      [since]
    );
    const [bySeverity] = await pool.execute(
      `SELECT v.severity, COUNT(*) as count FROM violation_details v
       JOIN audit_logs a ON v.audit_id = a.id
       WHERE a.created_at >= ?
       GROUP BY v.severity`,
      [since]
    );
    const dateGroupExpr = groupBy === 'hour'
      ? "DATE_FORMAT(a.created_at, '%Y-%m-%d %H:00')"
      : "DATE(a.created_at)";
    const [trend] = await pool.execute(
      `SELECT ${dateGroupExpr} as period, COUNT(*) as total,
       SUM(CASE WHEN a.status = 'passed' THEN 1 ELSE 0 END) as passed,
       SUM(CASE WHEN a.status = 'blocked' THEN 1 ELSE 0 END) as blocked,
       SUM(a.violation_lines) as violations
       FROM audit_logs a WHERE a.created_at >= ?
       GROUP BY period ORDER BY period ASC LIMIT 200`,
      [since]
    );
    const [summary] = await pool.execute(
      `SELECT COUNT(*) as total_audits,
       SUM(CASE WHEN status = 'passed' THEN 1 ELSE 0 END) as total_passed,
       SUM(CASE WHEN status = 'blocked' THEN 1 ELSE 0 END) as total_blocked,
       SUM(total_lines) as total_lines,
       SUM(masked_lines) as total_masked,
       SUM(violation_lines) as total_violations
       FROM audit_logs WHERE created_at >= ?`,
      [since]
    );

    res.json({
      code: 0,
      data: {
        summary: summary[0] || {},
        byStatus,
        byFormat,
        topRules,
        bySeverity,
        trend,
        since
      }
    });
  } catch (err) {
    console.error('[Stats] error:', err);
    res.status(500).json({ code: 500, message: err.message });
  }
}

module.exports = {
  uploadLog, previewLog, compareLog, maskLog, startMaskTask, getTaskStatus,
  listAuditLogs, getAuditDetail, downloadMasked, testMask, getStatistics
};
