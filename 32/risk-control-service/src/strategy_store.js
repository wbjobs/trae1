const { client } = require('./redis');
const { addListener } = require('./pubsub');
const DEFAULT_STRATEGIES = require('./config').strategies;

const STRATEGY_HASH_KEY = 'cfg:strategies';
const VERSION_LIST_KEY = 'cfg:strategy_versions';
const AUDIT_LIST_KEY = 'cfg:strategy_audit';
const INVALIDATE_CHANNEL = 'cfg:invalidate';
const MAX_VERSIONS = 20;
const TTL_MS = 2000;

const defaultSnapshot = () => ({
  rateLimit: { ...DEFAULT_STRATEGIES.rateLimit.defaults },
});

let cache = null;
let cacheAt = 0;

const loadFromRedis = async () => {
  const raw = await client.hget(STRATEGY_HASH_KEY, 'current');
  if (!raw) return defaultSnapshot();
  try {
    return JSON.parse(raw);
  } catch (_) {
    return defaultSnapshot();
  }
};

const getCurrent = async () => {
  const now = Date.now();
  if (cache && now - cacheAt < TTL_MS) return cache;
  cache = await loadFromRedis();
  cacheAt = now;
  return cache;
};

const getCurrentSync = () => cache || defaultSnapshot();

const invalidateCache = () => {
  cache = null;
  cacheAt = 0;
};

addListener(invalidateCache);

const listHistory = async (limit = MAX_VERSIONS) => {
  const raw = await client.lrange(VERSION_LIST_KEY, 0, limit - 1);
  return raw.map((r) => JSON.parse(r));
};

const listAudit = async (limit = 100) => {
  const raw = await client.lrange(AUDIT_LIST_KEY, 0, limit - 1);
  return raw.map((r) => JSON.parse(r));
};

const update = async ({ actionType, patch, operator, reason = '' }) => {
  const current = await getCurrent();
  const next = {
    rateLimit: { ...current.rateLimit },
  };
  const prevAction = next.rateLimit[actionType];
  if (!prevAction) {
    throw new Error(`unknown action_type: ${actionType}`);
  }
  const merged = { ...prevAction, ...patch };
  next.rateLimit[actionType] = merged;

  const version = {
    id: Date.now(),
    snapshot: next,
    operator,
    reason,
    changed_action: actionType,
    before: prevAction,
    after: merged,
    created_at: Date.now(),
  };

  const audit = {
    id: version.id,
    operator,
    action: 'UPDATE',
    target: actionType,
    before: prevAction,
    after: merged,
    reason,
    created_at: version.created_at,
  };

  const pipeline = client.pipeline();
  pipeline.hset(STRATEGY_HASH_KEY, 'current', JSON.stringify(next));
  pipeline.lpush(VERSION_LIST_KEY, JSON.stringify(version));
  pipeline.ltrim(VERSION_LIST_KEY, 0, MAX_VERSIONS - 1);
  pipeline.lpush(AUDIT_LIST_KEY, JSON.stringify(audit));
  pipeline.ltrim(AUDIT_LIST_KEY, 0, 200);
  await pipeline.exec();

  invalidateCache();
  await client.publish(INVALIDATE_CHANNEL, String(version.id));

  return version;
};

const rollback = async ({ operator, reason = 'rollback' }) => {
  const history = await listHistory(2);
  if (history.length < 2) {
    throw new Error('no previous version to rollback to');
  }
  const target = history[1];

  const audit = {
    id: Date.now(),
    operator,
    action: 'ROLLBACK',
    target_version: target.id,
    before: target.before,
    after: target.after,
    reason,
    created_at: Date.now(),
  };

  const pipeline = client.pipeline();
  pipeline.hset(STRATEGY_HASH_KEY, 'current', JSON.stringify(target.snapshot));
  pipeline.lpush(AUDIT_LIST_KEY, JSON.stringify(audit));
  pipeline.ltrim(AUDIT_LIST_KEY, 0, 200);
  await pipeline.exec();

  invalidateCache();
  await client.publish(INVALIDATE_CHANNEL, String(audit.id));

  return target;
};

const initialize = async () => {
  const exists = await client.hexists(STRATEGY_HASH_KEY, 'current');
  if (!exists) {
    const init = defaultSnapshot();
    const version = {
      id: Date.now(),
      snapshot: init,
      operator: 'system',
      reason: 'initial default configuration',
      changed_action: '*',
      before: null,
      after: init.rateLimit,
      created_at: Date.now(),
    };
    await client.hset(STRATEGY_HASH_KEY, 'current', JSON.stringify(init));
    await client.lpush(VERSION_LIST_KEY, JSON.stringify(version));
    console.log('[strategy] initialized with default configuration');
  } else {
    cache = await loadFromRedis();
    cacheAt = Date.now();
    console.log('[strategy] loaded existing configuration from redis');
  }
};

module.exports = {
  getCurrent,
  getCurrentSync,
  listHistory,
  listAudit,
  update,
  rollback,
  initialize,
  invalidateCache,
};
