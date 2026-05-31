const { OperationLog } = require('../models');

const operationLogger = (options = {}) => {
  return async (req, res, next) => {
    const startTime = Date.now();
    const originalSend = res.send.bind(res);

    const { module, action, description } = options;
    const logEntry = {
      userId: req.userId || null,
      username: req.username || null,
      module: module || extractModule(req.path),
      action: action || extractAction(req.method, req.path),
      description: description || '',
      method: req.method,
      requestUrl: req.originalUrl,
      requestParams: JSON.stringify(req.body) || JSON.stringify(req.query),
      ip: getClientIp(req),
      userAgent: req.headers['user-agent'] || '',
      duration: 0,
      status: 'success',
      errorMessage: null,
      responseData: null
    };

    res.send = function(data) {
      const duration = Date.now() - startTime;
      logEntry.duration = duration;

      try {
        const parsedData = JSON.parse(data);
        if (parsedData.code !== undefined && parsedData.code !== 200) {
          logEntry.status = 'error';
          logEntry.errorMessage = parsedData.message || '';
        }
        if (parsedData.code === 200) {
          logEntry.responseData = JSON.stringify(parsedData.data).substring(0, 2000);
        }
      } catch (e) {}

      saveOperationLog(logEntry).catch(err => {
        console.error('Failed to save operation log:', err);
      });

      return originalSend(data);
    };

    next();
  };
};

function getClientIp(req) {
  const xForwardedFor = req.headers['x-forwarded-for'];
  if (xForwardedFor) {
    return xForwardedFor.split(',')[0].trim();
  }
  return req.connection.remoteAddress || req.socket.remoteAddress || 'unknown';
}

function extractModule(path) {
  const pathSegments = path.split('/').filter(Boolean);
  return pathSegments[1] || 'system';
}

function extractAction(method, path) {
  if (path.includes('/login')) return 'login';
  if (path.includes('/logout')) return 'logout';
  switch (method.toLowerCase()) {
    case 'post': return 'create';
    case 'put': return 'update';
    case 'delete': return 'delete';
    case 'get': return 'query';
    default: return method.toLowerCase();
  }
}

async function saveOperationLog(logEntry) {
  try {
    await OperationLog.create(logEntry);
  } catch (error) {
    console.error('Save operation log error:', error);
  }
}

module.exports = {
  operationLogger
};
