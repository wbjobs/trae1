const { parseLine } = require('./logParser');

const VIOLATION_SEVERITY = {
  medium: 'medium',
  high: 'high',
  low: 'low'
};

function buildRegex(rule) {
  try {
    const flags = rule.case_sensitive ? 'g' : 'gi';
    return new RegExp(rule.pattern, flags);
  } catch {
    return null;
  }
}

function compileRules(rules) {
  const compiled = [];
  for (const rule of rules) {
    if (!rule.enabled) continue;
    const regex = buildRegex(rule);
    if (!regex) continue;
    compiled.push({
      id: rule.id,
      name: rule.name,
      regex,
      replacement: rule.replacement || '****',
      ruleType: rule.rule_type,
      priority: rule.priority || 0,
      scope: rule.scope || 'all'
    });
  }
  compiled.sort((a, b) => b.priority - a.priority);
  return compiled;
}

function applyMaskToText(text, compiledRules, { lineNumber = 0 } = {}) {
  if (!text || typeof text !== 'string') return { masked: text, violations: [], changed: false };
  const violations = [];
  let masked = text;
  let changed = false;
  for (const rule of compiledRules) {
    rule.regex.lastIndex = 0;
    let match;
    const localMatches = [];
    while ((match = rule.regex.exec(text)) !== null) {
      localMatches.push({ start: match.index, end: match.index + match[0].length, matched: match[0] });
      if (!rule.regex.global) break;
    }
    if (localMatches.length > 0) {
      changed = true;
      for (const m of localMatches) {
        violations.push({
          line_number: lineNumber,
          rule_id: rule.id,
          rule_name: rule.name,
          matched_text: m.matched.substring(0, 512),
          masked_text: rule.replacement.substring(0, 512),
          severity: rule.priority >= 90 ? 'high' : rule.priority >= 60 ? 'medium' : 'low'
        });
      }
      rule.regex.lastIndex = 0;
      masked = masked.replace(rule.regex, rule.replacement);
    }
  }
  return { masked, violations, changed };
}

function maskLine(rawLine, format, compiledRules, lineNumber) {
  const parsed = parseLine(rawLine, format);
  const fields = parsed.fields || {};
  const newFields = {};
  const allViolations = [];
  let fieldChanged = false;
  for (const key of Object.keys(fields)) {
    const val = fields[key];
    const result = applyMaskToText(val, compiledRules, { lineNumber });
    newFields[key] = result.masked;
    if (result.changed) {
      fieldChanged = true;
      result.violations.forEach(v => { v.field = key; });
      allViolations.push(...result.violations);
    }
  }
  const reconstructed = reconstructLine(parsed, newFields, format, rawLine);
  return {
    original: rawLine,
    masked: reconstructed,
    violations: allViolations,
    changed: fieldChanged,
    format
  };
}

function deepClone(obj) {
  if (obj === null || typeof obj !== 'object') return obj;
  if (Array.isArray(obj)) return obj.map(deepClone);
  const result = {};
  for (const k of Object.keys(obj)) result[k] = deepClone(obj[k]);
  return result;
}

function reconstructLine(parsed, newFields, format, originalRaw) {
  if (format === 'json' && parsed._json) {
    const obj = deepClone(parsed._json);
    for (const key of Object.keys(newFields)) {
      const pathParts = key.split('.');
      let cur = obj;
      let valid = true;
      for (let i = 0; i < pathParts.length - 1; i++) {
        if (cur == null || typeof cur !== 'object') { valid = false; break; }
        cur = cur[pathParts[i]];
      }
      if (valid && cur != null && typeof cur === 'object') {
        cur[pathParts[pathParts.length - 1]] = newFields[key];
      }
    }
    return JSON.stringify(obj);
  }
  if (format === 'csv' && parsed._parts) {
    const parts = parsed._parts.slice();
    for (const key of Object.keys(newFields)) {
      const idx = parseInt(key.replace('col', ''), 10);
      if (!isNaN(idx) && idx < parts.length) {
        let v = newFields[key];
        if (v.includes(',') || v.includes('"')) v = '"' + v.replace(/"/g, '""') + '"';
        parts[idx] = v;
      }
    }
    return parts.join(',');
  }
  if (format === 'nginx' && parsed.fields) {
    const f = { ...parsed.fields, ...newFields };
    return `${f.ip} - ${f.user} [${f.time}] "${f.request}" ${f.status} ${f.size} "${f.referer}" "${f.ua}"`;
  }
  if (format === 'apache' && parsed.fields) {
    const f = { ...parsed.fields, ...newFields };
    const rest = originalRaw.replace(/^\S+\s+\S+\s+\S+\s+\[[^\]]+\]\s+"[^"]*"\s+\d{3}\s+[\d-]+/, '');
    return `${f.ip} - ${f.user} [${f.time}] "${f.request}" ${f.status} ${f.size}${rest}`;
  }
  if (format === 'syslog' && parsed.fields) {
    const f = { ...parsed.fields, ...newFields };
    const pidPart = f.pid ? `[${f.pid}]` : '';
    return `${f.timestamp} ${f.host} ${f.program}${pidPart}: ${f.message}`;
  }
  return newFields.message || originalRaw;
}

function maskLineSimple(rawLine, compiledRules, lineNumber) {
  const result = applyMaskToText(rawLine, compiledRules, { lineNumber });
  return {
    original: rawLine,
    masked: result.masked,
    violations: result.violations,
    changed: result.changed
  };
}

module.exports = {
  compileRules,
  applyMaskToText,
  maskLine,
  maskLineSimple,
  VIOLATION_SEVERITY
};
