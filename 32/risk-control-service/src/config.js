module.exports = {
  grpc: {
    host: process.env.GRPC_HOST || '0.0.0.0',
    port: parseInt(process.env.GRPC_PORT || '50051', 10),
  },
  redis: {
    host: process.env.REDIS_HOST || '127.0.0.1',
    port: parseInt(process.env.REDIS_PORT || '6379', 10),
    db: parseInt(process.env.REDIS_DB || '0', 10),
    password: process.env.REDIS_PASSWORD || undefined,
  },
  kafka: {
    brokers: (process.env.KAFKA_BROKERS || 'localhost:9092').split(','),
    topic: process.env.KAFKA_TOPIC || 'risk-control-decisions',
    clientId: 'risk-control-service',
  },
  strategies: {
    rateLimit: {
      defaults: {
        login: { maxBurst: 5,  countPerPeriod: 5,  periodSeconds: 300 },
        order: { maxBurst: 10, countPerPeriod: 10, periodSeconds: 60 },
        post:  { maxBurst: 30, countPerPeriod: 30, periodSeconds: 60 },
      },
    },
    blacklist: {
      globalKey: 'blacklist:global',
      actionKeyPrefix: 'blacklist:action:',
      userKeyPrefix: 'blacklist:user:',
    },
  },
};
