const express = require('express');
const router = express.Router();
const cacheService = require('../services/cacheService');

router.get('/health', (req, res) => {
  const health = {
    status: 'healthy',
    timestamp: new Date().toISOString(),
    uptime: process.uptime(),
    memory: {
      total: process.memoryUsage().heapTotal,
      used: process.memoryUsage().heapUsed,
      external: process.memoryUsage().external
    },
    cpu: process.cpuUsage(),
    node: {
      version: process.version,
      env: process.env.NODE_ENV || 'development'
    },
    cache: cacheService.getStats()
  };

  res.json({
    code: 200,
    message: 'OK',
    data: health
  });
});

router.get('/cache/stats', (req, res) => {
  res.json({
    code: 200,
    message: 'OK',
    data: cacheService.getStats()
  });
});

router.post('/cache/clear', async (req, res) => {
  await cacheService.clear();
  res.json({
    code: 200,
    message: 'Cache cleared successfully'
  });
});

router.get('/config', (req, res) => {
  res.json({
    code: 200,
    message: 'OK',
    data: {
      appName: process.env.APP_NAME || 'MicroFrontend-API',
      version: process.env.VERSION || '1.0.0',
      environment: process.env.NODE_ENV || 'development'
    }
  });
});

module.exports = router;
