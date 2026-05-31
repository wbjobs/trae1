const path = require('path');
const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');
const config = require('./config');
const engine = require('./engine');
const strategyStore = require('./strategy_store');
const { publish } = require('./kafka');

const PROTO_PATH = path.join(__dirname, '..', 'proto', 'riskcontrol.proto');

const packageDefinition = protoLoader.loadSync(PROTO_PATH, {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
});

const protoDescriptor = grpc.loadPackageDefinition(packageDefinition);
const riskcontrol = protoDescriptor.riskcontrol;

let requestSeq = 0;
const nextId = () => {
  requestSeq = (requestSeq + 1) % Number.MAX_SAFE_INTEGER;
  return requestSeq;
};

const buildRateLimitConfig = (actionType, cfg) => ({
  action_type: actionType,
  max_burst: cfg.maxBurst,
  count_per_period: cfg.countPerPeriod,
  period_seconds: cfg.periodSeconds,
});

const handleCheck = async (call, callback) => {
  const start = process.hrtime.bigint();
  const req = call.request;

  try {
    const result = await engine.evaluate({
      user_id: req.user_id,
      action_type: req.action_type,
      timestamp: Number(req.timestamp) || Date.now(),
    });

    const end = process.hrtime.bigint();
    const latencyMs = Number(end - start) / 1e6;
    const requestId = nextId();

    const decisionEnum = riskcontrol.Decision[result.decision] || riskcontrol.Decision.PASS;
    const strategyEnum = riskcontrol.Strategy[result.strategy] || riskcontrol.Strategy.NONE;

    const response = {
      decision: decisionEnum,
      hit_strategy: strategyEnum,
      reason: result.reason,
      request_id: requestId,
      latency_ms: Math.round(latencyMs * 100) / 100,
    };

    setImmediate(() => {
      publish({
        request_id: requestId,
        user_id: req.user_id,
        action_type: req.action_type,
        timestamp: Number(req.timestamp) || Date.now(),
        decision: result.decision,
        strategy: result.strategy,
        reason: result.reason,
        latency_ms: response.latency_ms,
      }).catch((err) => console.error('[publish]', err));
    });

    callback(null, response);
  } catch (err) {
    console.error('[check] error:', err);
    callback({
      code: grpc.status.INTERNAL,
      message: err.message,
    });
  }
};

const handleCheckStream = (call) => {
  call.on('data', async (req) => {
    try {
      const result = await engine.evaluate({
        user_id: req.user_id,
        action_type: req.action_type,
        timestamp: Number(req.timestamp) || Date.now(),
      });
      call.write({
        decision: riskcontrol.Decision[result.decision] || riskcontrol.Decision.PASS,
        hit_strategy: riskcontrol.Strategy[result.strategy] || riskcontrol.Strategy.NONE,
        reason: result.reason,
      });
    } catch (err) {
      console.error('[checkStream] error:', err);
    }
  });
  call.on('end', () => call.end());
};

const handleGetStrategies = async (call, callback) => {
  try {
    const snapshot = await strategyStore.getCurrent();
    const rateLimits = Object.entries(snapshot.rateLimit || {}).map(([actionType, cfg]) =>
      buildRateLimitConfig(actionType, cfg),
    );
    callback(null, { rate_limits: rateLimits, updated_at: Date.now() });
  } catch (err) {
    console.error('[GetStrategies]', err);
    callback({ code: grpc.status.INTERNAL, message: err.message });
  }
};

const handleUpdateStrategy = async (call, callback) => {
  const req = call.request;
  const patch = {};
  if (req.max_burst !== undefined && req.max_burst !== 0) patch.maxBurst = Number(req.max_burst);
  if (req.count_per_period !== undefined && req.count_per_period !== 0) patch.countPerPeriod = Number(req.count_per_period);
  if (req.period_seconds !== undefined && req.period_seconds !== 0) patch.periodSeconds = Number(req.period_seconds);

  if (Object.keys(patch).length === 0) {
    return callback({ code: grpc.status.INVALID_ARGUMENT, message: 'no fields to update' });
  }

  try {
    const version = await strategyStore.update({
      actionType: req.action_type,
      patch,
      operator: req.operator || 'anonymous',
      reason: req.reason || '',
    });
    callback(null, {
      version_id: version.id,
      config: buildRateLimitConfig(req.action_type, version.after),
      created_at: version.created_at,
    });
  } catch (err) {
    console.error('[UpdateStrategy]', err);
    const code = /unknown action_type/.test(err.message)
      ? grpc.status.NOT_FOUND
      : grpc.status.INTERNAL;
    callback({ code, message: err.message });
  }
};

const handleListHistory = async (call, callback) => {
  try {
    const history = await strategyStore.listHistory(call.request.limit || 20);
    const entries = history.map((v) => ({
      id: v.id,
      operator: v.operator,
      action_type: v.changed_action,
      reason: v.reason,
      before: v.before ? buildRateLimitConfig(v.changed_action, v.before) : null,
      after: v.after ? buildRateLimitConfig(v.changed_action, v.after) : null,
      created_at: v.created_at,
    }));
    callback(null, { entries });
  } catch (err) {
    console.error('[ListHistory]', err);
    callback({ code: grpc.status.INTERNAL, message: err.message });
  }
};

const handleListAudit = async (call, callback) => {
  try {
    const audit = await strategyStore.listAudit(call.request.limit || 100);
    const entries = audit.map((a) => ({
      id: a.id,
      operator: a.operator,
      action: a.action,
      target: a.target,
      before_json: JSON.stringify(a.before),
      after_json: JSON.stringify(a.after),
      reason: a.reason,
      created_at: a.created_at,
    }));
    callback(null, { entries });
  } catch (err) {
    console.error('[ListAudit]', err);
    callback({ code: grpc.status.INTERNAL, message: err.message });
  }
};

const handleRollback = async (call, callback) => {
  try {
    const target = await strategyStore.rollback({
      operator: call.request.operator || 'anonymous',
      reason: call.request.reason || 'rollback',
    });
    callback(null, {
      rolled_back_to_version: target.id,
      action_type: target.changed_action,
      created_at: Date.now(),
    });
  } catch (err) {
    console.error('[Rollback]', err);
    const code = /no previous version/.test(err.message)
      ? grpc.status.FAILED_PRECONDITION
      : grpc.status.INTERNAL;
    callback({ code, message: err.message });
  }
};

const start = async () => {
  await strategyStore.initialize();

  const server = new grpc.Server();
  server.addService(riskcontrol.RiskControlService.service, {
    Check: handleCheck,
    CheckStream: handleCheckStream,
    GetStrategies: handleGetStrategies,
    UpdateStrategy: handleUpdateStrategy,
    ListHistory: handleListHistory,
    ListAudit: handleListAudit,
    Rollback: handleRollback,
  });

  const addr = `${config.grpc.host}:${config.grpc.port}`;
  server.bindAsync(addr, grpc.ServerCredentials.createInsecure(), (err, port) => {
    if (err) {
      console.error('[grpc] bind error:', err);
      process.exit(1);
    }
    console.log(`[grpc] risk-control-service listening on ${addr}`);
  });

  return server;
};

module.exports = { start, riskcontrol };
