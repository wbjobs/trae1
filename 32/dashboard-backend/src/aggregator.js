const client = require('./redis');

const BUCKET_KEY = 'agg:buckets';
const STRATEGY_KEY = 'agg:strategies';

const minuteBucket = (ts) => {
  const d = new Date(ts);
  d.setSeconds(0, 0);
  return d.getTime();
};

const record = async (event) => {
  const ts = event.timestamp || Date.now();
  const bucket = minuteBucket(ts);
  const field = String(bucket);

  const pipeline = client.pipeline();
  pipeline.hincrby(BUCKET_KEY, `t:${field}`, 1);
  pipeline.hincrby(BUCKET_KEY, `p:${field}`, event.decision === 'PASS' ? 1 : 0);
  pipeline.hincrby(BUCKET_KEY, `r:${field}`, event.decision === 'REJECT' ? 1 : 0);
  pipeline.hincrby(BUCKET_KEY, `v:${field}`, event.decision === 'VERIFY' ? 1 : 0);
  pipeline.hincrby(STRATEGY_KEY, event.strategy || 'NONE', 1);
  await pipeline.exec();

  const cutoff = Date.now() - config.aggregation.windowMinutes * 60 * 1000;
  cleanupOldBuckets(cutoff).catch((err) => console.error('[cleanup]', err));
};

const cleanupOldBuckets = async (cutoff) => {
  const all = await client.hkeys(BUCKET_KEY);
  const expired = all.filter((k) => {
    const ts = parseInt(k.split(':')[1], 10);
    return ts < cutoff;
  });
  if (expired.length > 0) {
    await client.hdel(BUCKET_KEY, ...expired);
  }
};

const getMetrics = async (sinceMinutes) => {
  const window = sinceMinutes || config.aggregation.windowMinutes;
  const cutoff = Date.now() - window * 60 * 1000;

  const buckets = await client.hgetall(BUCKET_KEY);
  const strategies = await client.hgetall(STRATEGY_KEY);

  const perMinute = Object.keys(buckets)
    .map((k) => {
      const [type, tsStr] = k.split(':');
      return { type, ts: parseInt(tsStr, 10), value: parseInt(buckets[k], 10) };
    })
    .filter((x) => x.ts >= cutoff);

  const tsSet = new Set(perMinute.map((x) => x.ts));
  const sortedTs = Array.from(tsSet).sort((a, b) => a - b);

  const series = sortedTs.map((ts) => {
    const total = perMinute.find((x) => x.ts === ts && x.type === 't')?.value || 0;
    const pass = perMinute.find((x) => x.ts === ts && x.type === 'p')?.value || 0;
    const reject = perMinute.find((x) => x.ts === ts && x.type === 'r')?.value || 0;
    const verify = perMinute.find((x) => x.ts === ts && x.type === 'v')?.value || 0;
    return {
      timestamp: ts,
      time: new Date(ts).toISOString().slice(11, 16),
      total, pass, reject, verify,
      rejectRate: total === 0 ? 0 : +(reject / total * 100).toFixed(2),
    };
  });

  const totalAll = series.reduce((a, b) => a + b.total, 0);
  const rejectAll = series.reduce((a, b) => a + b.reject, 0);
  const verifyAll = series.reduce((a, b) => a + b.verify, 0);

  return {
    window_minutes: window,
    generated_at: Date.now(),
    summary: {
      total: totalAll,
      pass: totalAll - rejectAll - verifyAll,
      reject: rejectAll,
      verify: verifyAll,
      rejectRate: totalAll === 0 ? 0 : +(rejectAll / totalAll * 100).toFixed(2),
    },
    per_minute: series,
    strategies: Object.keys(strategies).map((k) => ({
      strategy: k,
      count: parseInt(strategies[k], 10),
    })),
  };
};

module.exports = { record, getMetrics };
