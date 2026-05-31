const client = require('./redis');

const HASH_KEY = 'cfg:strategies';
const VERSION_KEY = 'cfg:strategy_versions';
const AUDIT_KEY = 'cfg:strategy_audit';
const INVALIDATE_CHANNEL = 'cfg:invalidate';
const MAX_VERSIONS = 20;

const listStrategies = async () => {
  const raw = await client.hget(HASH_KEY, 'current');
  if (!raw) return { rate_limits: [], updated_at: 0 };
  const snap = JSON.parse(raw);
  const rate_limits = Object.entries(snap.rateLimit || {}).map(([action_type, cfg]) => ({
    action_type,
    max_burst: cfg.maxBurst,
    count_per_period: cfg.countPerPeriod,
    period_seconds: cfg.periodSeconds,
  }));
  return { rate_limits, updated_at: Date.now() };
};

const updateStrategy = async ({ action_type, patch, operator, reason = '' }) => {
  const raw = await client.hget(HASH_KEY, 'current');
  if (!raw) throw new Error('strategy config not initialized');
  const snap = JSON.parse(raw);
  const prev = snap.rateLimit?.[action_type];
  if (!prev) throw new Error(`unknown action_type: ${action_type}`);
  const merged = { ...prev, ...patch };
  snap.rateLimit[action_type] = merged;

  const version = {
    id: Date.now(),
    snapshot: snap,
    operator,
    reason,
    changed_action: action_type,
    before: prev,
    after: merged,
    created_at: Date.now(),
  };
  const audit = {
    id: version.id,
    operator,
    action: 'UPDATE',
    target: action_type,
    before: prev,
    after: merged,
    reason,
    created_at: version.created_at,
  };

  const pipeline = client.pipeline();
  pipeline.hset(HASH_KEY, 'current', JSON.stringify(snap));
  pipeline.lpush(VERSION_KEY, JSON.stringify(version));
  pipeline.ltrim(VERSION_KEY, 0, MAX_VERSIONS - 1);
  pipeline.lpush(AUDIT_KEY, JSON.stringify(audit));
  pipeline.ltrim(AUDIT_KEY, 0, 200);
  await pipeline.exec();

  await client.publish(INVALIDATE_CHANNEL, String(version.id));

  return {
    version_id: version.id,
    config: {
      action_type,
      max_burst: merged.maxBurst,
      count_per_period: merged.countPerPeriod,
      period_seconds: merged.periodSeconds,
    },
    created_at: version.created_at,
  };
};

const listHistory = async (limit = MAX_VERSIONS) => {
  const raw = await client.lrange(VERSION_KEY, 0, limit - 1);
  return raw.map((r) => {
    const v = JSON.parse(r);
    return {
      id: v.id,
      operator: v.operator,
      action_type: v.changed_action,
      reason: v.reason,
      before: v.before ? {
        action_type: v.changed_action,
        max_burst: v.before.maxBurst,
        count_per_period: v.before.countPerPeriod,
        period_seconds: v.before.periodSeconds,
      } : null,
      after: v.after ? {
        action_type: v.changed_action,
        max_burst: v.after.maxBurst,
        count_per_period: v.after.countPerPeriod,
        period_seconds: v.after.periodSeconds,
      } : null,
      created_at: v.created_at,
    };
  });
};

const listAudit = async (limit = 100) => {
  const raw = await client.lrange(AUDIT_KEY, 0, limit - 1);
  return raw.map((r) => {
    const a = JSON.parse(r);
    return {
      id: a.id,
      operator: a.operator,
      action: a.action,
      target: a.target,
      before_json: JSON.stringify(a.before),
      after_json: JSON.stringify(a.after),
      reason: a.reason,
      created_at: a.created_at,
    };
  });
};

const rollback = async ({ operator, reason = 'rollback' }) => {
  const raw = await client.lrange(VERSION_KEY, 0, 1);
  if (raw.length < 2) throw new Error('no previous version to rollback to');
  const target = JSON.parse(raw[1]);

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
  pipeline.hset(HASH_KEY, 'current', JSON.stringify(target.snapshot));
  pipeline.lpush(AUDIT_KEY, JSON.stringify(audit));
  pipeline.ltrim(AUDIT_KEY, 0, 200);
  await pipeline.exec();

  await client.publish(INVALIDATE_CHANNEL, String(audit.id));

  return {
    rolled_back_to_version: target.id,
    action_type: target.changed_action,
    created_at: Date.now(),
  };
};

module.exports = {
  listStrategies,
  updateStrategy,
  listHistory,
  listAudit,
  rollback,
};
