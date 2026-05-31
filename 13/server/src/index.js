const express = require('express');
const cors = require('cors');
const config = require('../config');
const ruleRoutes = require('./routes/ruleRoutes');
const logRoutes = require('./routes/logRoutes');

const app = express();

app.use(cors());
app.use(express.json({ limit: '10mb' }));
app.use(express.urlencoded({ extended: true, limit: '10mb' }));

app.use('/api/rules', ruleRoutes);
app.use('/api/logs', logRoutes);

app.get('/api/health', (req, res) => {
  res.json({ code: 0, data: { status: 'ok', time: new Date().toISOString() } });
});

app.use((err, req, res, next) => {
  console.error('[Server] error:', err);
  if (err.message && err.message.includes('File too large')) {
    return res.status(413).json({ code: 413, message: '文件过大，最大支持 ' + (config.upload.maxFileSize / 1024 / 1024) + 'MB' });
  }
  res.status(500).json({ code: 500, message: err.message || '服务器错误' });
});

app.listen(config.port, () => {
  console.log(`[Server] 日志脱敏服务已启动: http://localhost:${config.port}`);
});
