const Redis = require('ioredis');
const config = require('./config');

const client = new Redis({
  host: config.redis.host,
  port: config.redis.port,
  db: config.redis.db,
  password: config.redis.password,
});

client.on('error', (err) => {
  console.error('[redis]', err.message);
});

module.exports = client;
