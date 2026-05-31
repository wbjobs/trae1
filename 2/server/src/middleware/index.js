const errorHandler = async (ctx, next) => {
  try {
    await next();
    
    if (ctx.status === 404 && !ctx.body) {
      ctx.body = {
        code: 404,
        message: '资源不存在'
      };
    }
  } catch (error) {
    console.error('Error:', error);
    
    ctx.status = error.status || 500;
    ctx.body = {
      code: ctx.status,
      message: error.message || '服务器内部错误',
      ...(process.env.NODE_ENV === 'development' && { stack: error.stack })
    };
  }
};

const requestLogger = async (ctx, next) => {
  const start = Date.now();
  
  await next();
  
  const duration = Date.now() - start;
  console.log(`${ctx.method} ${ctx.url} - ${ctx.status} - ${duration}ms`);
};

const validateRequest = (schema, property = 'body') => {
  return async (ctx, next) => {
    try {
      const data = ctx[property];
      await schema.validateAsync(data, { abortEarly: false });
      await next();
    } catch (error) {
      ctx.status = 400;
      ctx.body = {
        code: 400,
        message: '请求参数验证失败',
        details: error.details ? error.details.map(d => d.message) : [error.message]
      };
    }
  };
};

const corsHandler = async (ctx, next) => {
  ctx.set('Access-Control-Allow-Origin', '*');
  ctx.set('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
  ctx.set('Access-Control-Allow-Headers', 'Content-Type, Authorization');
  ctx.set('Access-Control-Max-Age', '3600');
  
  if (ctx.method === 'OPTIONS') {
    ctx.status = 204;
    return;
  }
  
  await next();
};

const rateLimiter = (windowMs = 60000, maxRequests = 100) => {
  const requests = new Map();
  
  return async (ctx, next) => {
    const ip = ctx.ip || ctx.request.ip;
    const now = Date.now();
    const windowStart = now - windowMs;
    
    let requestLog = requests.get(ip) || [];
    requestLog = requestLog.filter(time => time > windowStart);
    
    if (requestLog.length >= maxRequests) {
      ctx.status = 429;
      ctx.body = {
        code: 429,
        message: '请求过于频繁，请稍后再试'
      };
      return;
    }
    
    requestLog.push(now);
    requests.set(ip, requestLog);
    
    await next();
  };
};

module.exports = {
  errorHandler,
  requestLogger,
  validateRequest,
  corsHandler,
  rateLimiter
};
