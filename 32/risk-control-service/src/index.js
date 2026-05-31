const server = require('./server');

server.start();

process.on('uncaughtException', (err) => {
  console.error('[fatal] uncaughtException:', err);
});

process.on('unhandledRejection', (err) => {
  console.error('[fatal] unhandledRejection:', err);
});
