const kafka = require('./kafka');
const http = require('./server');

(async () => {
  try {
    await kafka.start();
    http.start();
  } catch (err) {
    console.error('[fatal]', err);
    process.exit(1);
  }
})();
