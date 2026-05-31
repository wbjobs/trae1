const { Kafka } = require('kafkajs');
const config = require('./config');

const kafka = new Kafka({
  clientId: config.kafka.clientId,
  brokers: config.kafka.brokers,
  retry: {
    initialRetryTime: 100,
    retries: 8,
  },
});

const producer = kafka.producer({
  allowAutoTopicCreation: true,
  transactionTimeout: 30000,
});

const started = producer.connect()
  .then(() => console.log('[kafka] producer connected'))
  .catch((err) => console.error('[kafka] producer connect error:', err.message));

const publish = async (event) => {
  await started;
  try {
    await producer.send({
      topic: config.kafka.topic,
      messages: [
        {
          key: event.user_id || undefined,
          value: JSON.stringify(event),
          timestamp: String(Date.now()),
        },
      ],
    });
  } catch (err) {
    console.error('[kafka] publish error:', err.message);
  }
};

process.on('SIGTERM', async () => {
  try { await producer.disconnect(); } catch (_) {}
});

module.exports = { kafka, producer, publish };
