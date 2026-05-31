const Redis = require('ioredis');
const fs = require('fs');
const path = require('path');
const config = require('./config');

const client = new Redis({
  host: config.redis.host,
  port: config.redis.port,
  db: config.redis.db,
  password: config.redis.password,
  enableReadyCheck: true,
  maxRetriesPerRequest: 3,
  lazyConnect: false,
});

const subscriber = new Redis({
  host: config.redis.host,
  port: config.redis.port,
  db: config.redis.db,
  password: config.redis.password,
});

client.on('error', (err) => {
  console.error('[redis] connection error:', err.message);
});

client.on('connect', () => {
  console.log('[redis] connected to', config.redis.host + ':' + config.redis.port);
});

subscriber.on('error', (err) => {
  console.error('[redis-sub] error:', err.message);
});

const luaDir = path.join(__dirname, '..', 'lua');

const loadScript = (name, filename) => {
  const source = fs.readFileSync(path.join(luaDir, filename), 'utf8');
  client.defineCommand(name, { lua: source });
  return name;
};

loadScript('blacklistCheck', 'blacklist.lua');
loadScript('fixedWindow', 'fixed_window.lua');

let cellAvailable = false;

const detectCell = async () => {
  try {
    const info = await client.call('MODULE', 'LIST');
    if (Array.isArray(info)) {
      for (const mod of info) {
        if (mod && mod.name && mod.name.toLowerCase() === 'cell') {
          cellAvailable = true;
          console.log('[redis] redis-cell detected (CL.THROTTLE active)');
          return true;
        }
      }
    }
  } catch (_) {}
  console.log('[redis] redis-cell not found, falling back to fixed-window Lua counter');
  return false;
};

const isCellAvailable = () => cellAvailable;

const clThrottle = async (key, maxBurst, countPerPeriod, periodSeconds, quota = 1) => {
  const result = await client.call(
    'CL.THROTTLE',
    key,
    maxBurst,
    countPerPeriod,
    periodSeconds,
    quota,
  );
  return {
    throttled: result[0] === 1,
    limit: result[1],
    remaining: result[2],
    resetSec: result[3],
    retrySec: result[4],
  };
};

client.on('ready', () => {
  detectCell().catch((err) => console.error('[redis] cell detection failed:', err.message));
});

module.exports = { client, subscriber, isCellAvailable, clThrottle };
