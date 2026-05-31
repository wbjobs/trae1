const Koa = require('koa');
const cors = require('@koa/cors');
const { koaBody } = require('koa-body');
const path = require('path');
const sequelize = require('./config/database');
require('./models');

const fileRoutes = require('./routes/file');
const shareRoutes = require('./routes/share');
const chunkRoutes = require('./routes/chunk');
const CleanTask = require('./tasks/cleanExpired');
const TempCleanup = require('./tasks/tempCleanup');
const { getLogger } = require('./utils/logger');
const { rateLimitMiddleware, uploadLimiter, apiLimiter, shareAccessLimiter } = require('./middleware/rateLimit');

const logger = getLogger('app');

const app = new Koa();
const PORT = process.env.PORT || 3000;

app.use(cors({
  origin: '*',
  allowMethods: ['GET', 'POST', 'PUT', 'DELETE', 'OPTIONS'],
  allowHeaders: ['Content-Type', 'Authorization', 'Accept']
}));

app.use(koaBody({
  multipart: true,
  formidable: {
    uploadDir: path.join(__dirname, '../uploads'),
    maxFileSize: 500 * 1024 * 1024,
    keepExtensions: true
  },
  jsonLimit: '10mb',
  formLimit: '10mb'
}));

app.use(async (ctx, next) => {
  const start = Date.now();
  logger.info(`${ctx.method} ${ctx.url} - Start`);
  
  try {
    await next();
    const duration = Date.now() - start;
    logger.info(`${ctx.method} ${ctx.url} - Completed - ${duration}ms - Status: ${ctx.status}`);
  } catch (error) {
    const duration = Date.now() - start;
    logger.error(`${ctx.method} ${ctx.url} - Error - ${duration}ms`, {
      message: error.message,
      stack: error.stack
    });
    ctx.status = error.status || 500;
    ctx.body = {
      code: ctx.status,
      message: error.message || '服务器内部错误'
    };
  }
});

app.use(rateLimitMiddleware(apiLimiter));

app.use(fileRoutes.routes());
app.use(fileRoutes.allowedMethods());
app.use(shareRoutes.routes());
app.use(shareRoutes.allowedMethods());
app.use(chunkRoutes.routes());
app.use(chunkRoutes.allowedMethods());

async function startServer() {
  try {
    await sequelize.authenticate();
    logger.info('数据库连接成功');

    await sequelize.sync({ alter: true });
    logger.info('数据库表同步完成');

    CleanTask.start();
    TempCleanup.start();

    app.listen(PORT, () => {
      logger.info(`服务器运行在 http://localhost:${PORT}`);
    });
  } catch (error) {
    logger.error('服务器启动失败:', error);
    process.exit(1);
  }
}

process.on('uncaughtException', (error) => {
  logger.error('未捕获的异常:', error);
});

process.on('unhandledRejection', (reason, promise) => {
  logger.error('未处理的Promise拒绝:', reason);
});

startServer();
