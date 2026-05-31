const { Kafka } = require('kafkajs');
const config = require('./config');
const { record } = require('./aggregator');

const kafka = new Kafka({
  clientId: 'dashboard-backend',
  brokers: config.kafka.brokers,
});

const consumer = kafka.consumer({
  groupId: config.kafka.consumerGroup,
});

let ready = false;

const start = async () => {
  await consumer.connect();
  await consumer.subscribe({
    topic: config.kafka.topic,
    fromBeginning: false,
  });
  console.log('[kafka] consumer subscribed to', config.kafka.topic);

  await consumer.run({
    eachMessage: async ({ message }) => {
      try {
        const event = JSON.parse(message.value.toString());
        await record(event);
      } catch (err) {
        console.error('[consumer] parse error:', err.message);
      }
    },
  });
  ready = true;
};

const status = () => ({ ready });

process.on('SIGTERM', async () => {
  try { await consumer.disconnect(); } catch (_) {}
});

module.exports = { start, status };
