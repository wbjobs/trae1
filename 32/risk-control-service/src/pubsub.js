const { subscriber } = require('./redis');

const INVALIDATE_CHANNEL = 'cfg:invalidate';

const listeners = new Set();

const addListener = (fn) => {
  listeners.add(fn);
  return () => listeners.delete(fn);
};

subscriber.on('message', (channel) => {
  if (channel === INVALIDATE_CHANNEL) {
    for (const fn of listeners) {
      try { fn(); } catch (err) { console.error('[pubsub] listener error:', err.message); }
    }
  }
});

subscriber.subscribe(INVALIDATE_CHANNEL).catch((err) => {
  if (!String(err.message).includes('only')) {
    console.error('[pubsub] subscribe error:', err.message);
  }
});

module.exports = { addListener, INVALIDATE_CHANNEL };
