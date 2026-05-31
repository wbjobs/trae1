const http = require('http');
const config = require('./config');
const { getMetrics } = require('./aggregator');
const strategyAdmin = require('./strategy_admin');

const sendJson = (res, status, body) => {
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
  });
  res.end(JSON.stringify(body));
};

const parseQuery = (url) => {
  const q = {};
  const idx = url.indexOf('?');
  if (idx === -1) return q;
  const pairs = url.slice(idx + 1).split('&');
  for (const p of pairs) {
    if (!p) continue;
    const [k, v] = p.split('=');
    q[decodeURIComponent(k)] = decodeURIComponent(v || '');
  }
  return q;
};

const readBody = (req) => new Promise((resolve, reject) => {
  let data = '';
  req.on('data', (chunk) => { data += chunk; });
  req.on('end', () => {
    if (!data) return resolve({});
    try { resolve(JSON.parse(data)); } catch (err) { reject(err); }
  });
  req.on('error', reject);
});

const createServer = () => {
  return http.createServer(async (req, res) => {
    if (req.method === 'OPTIONS') {
      sendJson(res, 204, {});
      return;
    }

    try {
      if (req.url.startsWith('/api/health')) {
        sendJson(res, 200, { status: 'ok', uptime: process.uptime() });
        return;
      }

      if (req.url.startsWith('/api/metrics')) {
        const q = parseQuery(req.url);
        const since = q.window ? parseInt(q.window, 10) : undefined;
        const metrics = await getMetrics(since);
        sendJson(res, 200, metrics);
        return;
      }

      if (req.url === '/api/strategies' && req.method === 'GET') {
        const data = await strategyAdmin.listStrategies();
        sendJson(res, 200, data);
        return;
      }

      if (req.url === '/api/strategies' && req.method === 'POST') {
        const body = await readBody(req);
        const { action_type, max_burst, count_per_period, period_seconds, operator, reason } = body;
        if (!action_type) return sendJson(res, 400, { error: 'action_type required' });
        const patch = {};
        if (max_burst !== undefined && max_burst !== null) patch.maxBurst = Number(max_burst);
        if (count_per_period !== undefined && count_per_period !== null) patch.countPerPeriod = Number(count_per_period);
        if (period_seconds !== undefined && period_seconds !== null) patch.periodSeconds = Number(period_seconds);
        if (Object.keys(patch).length === 0) return sendJson(res, 400, { error: 'no fields to update' });
        const version = await strategyAdmin.updateStrategy({
          action_type, patch, operator: operator || 'anonymous', reason: reason || '',
        });
        sendJson(res, 200, version);
        return;
      }

      if (req.url === '/api/strategies/history' && req.method === 'GET') {
        const q = parseQuery(req.url);
        const limit = q.limit ? parseInt(q.limit, 10) : 20;
        const entries = await strategyAdmin.listHistory(limit);
        sendJson(res, 200, { entries });
        return;
      }

      if (req.url === '/api/strategies/audit' && req.method === 'GET') {
        const q = parseQuery(req.url);
        const limit = q.limit ? parseInt(q.limit, 10) : 100;
        const entries = await strategyAdmin.listAudit(limit);
        sendJson(res, 200, { entries });
        return;
      }

      if (req.url === '/api/strategies/rollback' && req.method === 'POST') {
        const body = await readBody(req);
        const result = await strategyAdmin.rollback({
          operator: body.operator || 'anonymous',
          reason: body.reason || 'rollback',
        });
        sendJson(res, 200, result);
        return;
      }

      sendJson(res, 404, { error: 'not found' });
    } catch (err) {
      console.error('[http]', err);
      sendJson(res, 500, { error: err.message });
    }
  });
};

const start = () => {
  const server = createServer();
  server.listen(config.http.port, config.http.host, () => {
    console.log(`[http] dashboard-backend listening on ${config.http.host}:${config.http.port}`);
  });
  return server;
};

module.exports = { start };
