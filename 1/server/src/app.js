const express = require('express');
const cors = require('cors');
const helmet = require('helmet');
const morgan = require('morgan');
const config = require('./config');
const { testConnection, syncDatabase } = require('./config/database');
const { errorHandler, notFoundHandler } = require('./middleware/errorHandler');
const cacheService = require('./services/cacheService');
const { cacheMiddleware, cacheConfig } = require('./middleware/cache');

const app = express();

app.use(helmet());
app.use(cors(config.cors));
app.use(express.json({ limit: '10mb' }));
app.use(express.urlencoded({ extended: true, limit: '10mb' }));
app.use(morgan(config.logging.level === 'debug' ? 'dev' : 'combined'));

app.use((req, res, next) => {
  res.setHeader('X-Powered-By', 'MicroFrontend-API/1.0');
  res.setHeader('X-Response-Time', new Date().toISOString());
  next();
});

app.get('/', (req, res) => {
  res.json({
    code: 200,
    message: '微前端权限管理系统 API 服务运行正常',
    timestamp: new Date().toISOString(),
    version: '1.0.0',
    cache: cacheService.getStats()
  });
});

app.get('/health', (req, res) => {
  res.json({
    code: 200,
    status: 'healthy',
    timestamp: new Date().toISOString(),
    uptime: process.uptime(),
    memory: process.memoryUsage(),
    cache: cacheService.getStats()
  });
});

app.get('/api/cache/stats', (req, res) => {
  res.json({
    code: 200,
    data: cacheService.getStats()
  });
});

app.post('/api/cache/clear', async (req, res) => {
  await cacheService.clear();
  res.json({
    code: 200,
    message: 'Cache cleared successfully'
  });
});

app.use('/api/permissions', cacheMiddleware(cacheConfig.permissions));
app.use('/api/roles', cacheMiddleware(cacheConfig.roles));
app.use('/api/system-config', cacheMiddleware(cacheConfig.config));

app.use('/api', require('./routes/auth'));
app.use('/api', require('./routes/user'));
app.use('/api', require('./routes/role'));
app.use('/api', require('./routes/permission'));
app.use('/api', require('./routes/log'));
app.use('/api', require('./routes/systemConfig'));
app.use('/api', require('./routes/health'));

app.use(notFoundHandler);
app.use(errorHandler);

const startServer = async () => {
  try {
    await testConnection();
    await syncDatabase();

    app.listen(config.port, () => {
      console.log(`Server is running on port ${config.port}`);
      console.log(`Environment: ${config.env}`);
      console.log(`Cache enabled: true`);
    });
  } catch (error) {
    console.error('Failed to start server:', error);
    process.exit(1);
  }
};

startServer();

module.exports = app;
