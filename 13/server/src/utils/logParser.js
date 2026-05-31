const readline = require('readline');
const fs = require('fs');
const path = require('path');

const FORMAT_PARSERS = {
  plain: (line) => ({ raw: line, fields: { message: line } }),

  json: (line) => {
    const trimmed = line.trim();
    if (!trimmed) return { raw: line, fields: {} };
    try {
      const obj = JSON.parse(trimmed);
      const flat = {};
      const flatten = (o, prefix = '') => {
        if (o && typeof o === 'object' && !Array.isArray(o)) {
          for (const k of Object.keys(o)) {
            flatten(o[k], prefix ? prefix + '.' + k : k);
          }
        } else {
          flat[prefix] = String(o ?? '');
        }
      };
      flatten(obj);
      return { raw: line, fields: flat, _json: obj };
    } catch {
      return { raw: line, fields: { message: line } };
    }
  },

  csv: (line) => {
    const fields = {};
    const parts = parseCSV(line);
    parts.forEach((v, i) => { fields['col' + i] = v; });
    return { raw: line, fields, _parts: parts };
  },

  nginx: (line) => {
    const regex = /^(\S+)\s+-\s+(\S+)\s+\[([^\]]+)\]\s+"([^"]*)"\s+(\d{3})\s+(\d+|-)\s+"([^"]*)"\s+"([^"]*)"$/;
    const m = line.match(regex);
    if (m) {
      return {
        raw: line,
        fields: {
          ip: m[1], user: m[2], time: m[3],
          request: m[4], status: m[5], size: m[6],
          referer: m[7], ua: m[8]
        }
      };
    }
    return { raw: line, fields: { message: line } };
  },

  syslog: (line) => {
    const regex = /^(\w{3}\s+\d+\s+[\d:]+)\s+(\S+)\s+([^:\[]+)(?:\[(\d+)\])?:\s*(.*)$/;
    const m = line.match(regex);
    if (m) {
      return {
        raw: line,
        fields: {
          timestamp: m[1], host: m[2], program: m[3], pid: m[4] || '', message: m[5]
        }
      };
    }
    return { raw: line, fields: { message: line } };
  },

  apache: (line) => {
    const regex = /^(\S+)\s+\S+\s+(\S+)\s+\[([^\]]+)\]\s+"([^"]*)"\s+(\d{3})\s+(\d+|-)/;
    const m = line.match(regex);
    if (m) {
      return {
        raw: line,
        fields: {
          ip: m[1], user: m[2], time: m[3], request: m[4], status: m[5], size: m[6]
        }
      };
    }
    return { raw: line, fields: { message: line } };
  }
};

function parseCSV(line) {
  const result = [];
  let cur = '';
  let inQuote = false;
  for (let i = 0; i < line.length; i++) {
    const ch = line[i];
    if (ch === '"') {
      if (inQuote && line[i + 1] === '"') { cur += '"'; i++; }
      else { inQuote = !inQuote; }
    } else if (ch === ',' && !inQuote) {
      result.push(cur);
      cur = '';
    } else {
      cur += ch;
    }
  }
  result.push(cur);
  return result;
}

function parseLine(line, format) {
  const parser = FORMAT_PARSERS[format] || FORMAT_PARSERS.plain;
  return parser(line);
}

async function readLines(filePath, { maxLines = 500 } = {}) {
  return new Promise((resolve, reject) => {
    const lines = [];
    const rl = readline.createInterface({
      input: fs.createReadStream(filePath, { highWaterMark: 64 * 1024 }),
      crlfDelay: Infinity
    });
    rl.on('line', (line) => {
      if (lines.length < maxLines) lines.push(line);
      else { rl.close(); }
    });
    rl.on('close', () => resolve(lines));
    rl.on('error', reject);
  });
}

async function countLines(filePath) {
  return new Promise((resolve, reject) => {
    let count = 0;
    const rl = readline.createInterface({
      input: fs.createReadStream(filePath, { highWaterMark: 256 * 1024 }),
      crlfDelay: Infinity
    });
    rl.on('line', () => { count++; });
    rl.on('close', () => resolve(count));
    rl.on('error', reject);
  });
}

function detectFormat(sampleLines) {
  if (!sampleLines || sampleLines.length === 0) return 'plain';
  const sample = sampleLines[0] || '';
  const trimmed = sample.trim();
  if (trimmed.startsWith('{') || trimmed.startsWith('[')) return 'json';
  if (sample.split(',').length >= 3 && /"[^"]*"/.test(sample)) return 'csv';
  if (/^\S+\s+-\s+\S+\s+\[/.test(sample)) return 'nginx';
  if (/^\w{3}\s+\d+\s+[\d:]+\s+\S+\s+\w+\[\d+\]/.test(sample)) return 'syslog';
  if (/^\S+\s+\S+\s+\S+\s+\[/.test(sample)) return 'apache';
  return 'plain';
}

module.exports = { parseLine, readLines, countLines, detectFormat, FORMAT_PARSERS };
