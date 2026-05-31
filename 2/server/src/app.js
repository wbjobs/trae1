const Koa = require('koa');
const http = require('http');
const dotenv = require('dotenv');
const bodyParser = require('koa-bodyparser');
const serve = require('koa-static');
const path = require('path');

const { connectDB } = require('./config/db');
const { initSocketService } = require('./socket');
const { errorHandler, requestLogger, corsHandler, rateLimiter } = require('./middleware');

const authRoutes = require('./routes/authRoutes');
const documentRoutes = require('./routes/documentRoutes');
const annotationRoutes = require('./routes/annotationRoutes');
const permissionRoutes = require('./routes/permissionRoutes');

dotenv.config();

const app = new Koa();
const server = http.createServer(app.callback());

initSocketService(server);

app.use(errorHandler);
app.use(requestLogger);
app.use(corsHandler);
app.use(rateLimiter(60000, 200));
app.use(bodyParser({
  jsonLimit: '10mb',
  textLimit: '10mb'
}));

app.use(serve(path.join(__dirname, 'public')));

app.use(authRoutes.routes());
app.use(authRoutes.allowedMethods());

app.use(documentRoutes.routes());
app.use(documentRoutes.allowedMethods());

app.use(annotationRoutes.routes());
app.use(annotationRoutes.allowedMethods());

app.use(permissionRoutes.routes());
app.use(permissionRoutes.allowedMethods());

app.use(async (ctx) => {
  ctx.status = 404;
  ctx.body = {
    code: 404,
    message: '接口不存在'
  };
});

const PORT = process.env.PORT || 3000;

const startServer = async () => {
  try {
    await connectDB();
    
    server.listen(PORT, () => {
      console.log(`Server running on port ${PORT}`);
      console.log(`Environment: ${process.env.NODE_ENV}`);
      console.log(`MongoDB: ${process.env.MONGODB_URI}`);
    });
  } catch (error) {
    console.error('Failed to start server:', error);
    process.exit(1);
  }
};

const gracefulShutdown = async (signal) => {
  console.log(`\n${signal} received. Starting graceful shutdown...`);
  
  server.close(async () => {
    console.log('HTTP server closed');
    
    try {
      const { closeDB } = require('./config/db');
      await closeDB();
      console.log('Database connection closed');
    } catch (error) {
      console.error('Error closing database:', error);
    }
    
    console.log('Server shutdown complete');
    process.exit(0);
  });
  
  setTimeout(() => {
    console.error('Forced shutdown after timeout');
    process.exit(1);
  }, 10000);
};

process.on('SIGTERM', () => gracefulShutdown('SIGTERM'));
process.on('SIGINT', () => gracefulShutdown('SIGINT'));

process.on('uncaughtException', (error) => {
  console.error('Uncaught Exception:', error);
  gracefulShutdown('uncaughtException');
});

process.on('unhandledRejection', (reason, promise) => {
  console.error('Unhandled Rejection:', reason);
});

if (require.main === module) {
  startServer();
}

module.exports = app;
