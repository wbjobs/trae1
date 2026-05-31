module.exports = {
  http: {
    host: process.env.HTTP_HOST || '0.0.0.0',
    port: parseInt(process.env.HTTP_PORT || '4000', 10),
  },
  redis: {
    host: process.env.REDIS_HOST || '127.0.0.1',
    port: parseInt(process.env.REDIS_PORT || '6379', 10),
    db: parseInt(process.env.REDIS_DB || '1', 10),
    password: process.env.REDIS_PASSWORD || undefined,
  },
  kafka: {
    brokers: (process.env.KAFKA_BROKERS || 'localhost:9092').split(','),
    topic: process.env.KAFKA_TOPIC || 'risk-control-decisions',
    consumerGroup: 'dashboard-aggregator',
  },
  aggregation: {
    windowMinutes: parseInt(process.env.AGG_WINDOW_MINUTES || '60', 10),
  },
};
