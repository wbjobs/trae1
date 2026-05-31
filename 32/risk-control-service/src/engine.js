const redis = require('./redis');
const strategyStore = require('./strategy_store');

const Decision = {
  PASS: 'PASS',
  REJECT: 'REJECT',
  VERIFY: 'VERIFY',
};

const Strategy = {
  NONE: 'NONE',
  RATE_LIMIT: 'RATE_LIMIT',
  BLACKLIST: 'BLACKLIST',
};

const FALLBACK_CFG = { maxBurst: 60, countPerPeriod: 60, periodSeconds: 60 };

const getRateLimitConfig = (snapshot, actionType) => {
  const cfg = snapshot?.rateLimit?.[actionType];
  if (cfg) return cfg;
  return FALLBACK_CFG;
};

const checkBlacklist = async (userId, actionType) => {
  const globalKey = 'blacklist:global';
  const actionKey = 'blacklist:action:' + actionType;
  const userKey = 'blacklist:user:' + userId;
  const result = await redis.client.blacklistCheck(3, globalKey, actionKey, userKey, userId, actionType);
  return { blocked: result[0] === '1', reason: result[1] };
};

const checkRateLimitCell = async (userId, actionType, cfg) => {
  const key = `rl:${actionType}:${userId}`;
  const r = await redis.clThrottle(key, cfg.maxBurst, cfg.countPerPeriod, cfg.periodSeconds, 1);
  return { throttled: r.throttled, remaining: r.remaining, limit: r.limit, retrySec: r.retrySec, config: cfg };
};

const checkRateLimitFallback = async (userId, actionType, cfg) => {
  const key = `fw:${actionType}:${userId}`;
  const windowSec = cfg.periodSeconds;
  const maxReq = cfg.maxBurst;
  const result = await redis.client.fixedWindow(1, key, windowSec, maxReq);
  const allowed = result[0] === '1';
  const count = parseInt(result[1], 10);
  return {
    throttled: !allowed,
    remaining: Math.max(0, maxReq - count),
    limit: maxReq,
    retrySec: allowed ? 0 : parseInt(result[2], 10),
    config: cfg,
  };
};

const checkRateLimit = async (userId, actionType, cfg) => {
  if (redis.isCellAvailable()) {
    return checkRateLimitCell(userId, actionType, cfg);
  }
  return checkRateLimitFallback(userId, actionType, cfg);
};

const evaluate = async ({ user_id, action_type }) => {
  const blacklist = await checkBlacklist(user_id, action_type);
  if (blacklist.blocked) {
    return {
      decision: Decision.REJECT,
      strategy: Strategy.BLACKLIST,
      reason: blacklist.reason || 'user blacklisted',
    };
  }

  const snapshot = strategyStore.getCurrentSync();
  const cfg = getRateLimitConfig(snapshot, action_type);

  const rl = await checkRateLimit(user_id, action_type, cfg);
  if (rl.throttled) {
    return {
      decision: Decision.REJECT,
      strategy: Strategy.RATE_LIMIT,
      reason: `rate limit exceeded (${rl.config.maxBurst}/${rl.config.periodSeconds}s), retry in ${rl.retrySec}s`,
    };
  }

  const nearLimit = rl.remaining <= Math.ceil(rl.limit * 0.2);
  if (nearLimit) {
    return {
      decision: Decision.VERIFY,
      strategy: Strategy.RATE_LIMIT,
      reason: `approaching rate limit (${rl.remaining}/${rl.limit} remaining)`,
    };
  }

  return {
    decision: Decision.PASS,
    strategy: Strategy.NONE,
    reason: 'ok',
  };
};

module.exports = { evaluate, Decision, Strategy };
