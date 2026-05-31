const cacheService = require('../services/cacheService');

const DEFAULT_CACHE_TTL = 60;

const cacheMiddleware = (options = {}) => {
  const {
    ttl = DEFAULT_CACHE_TTL,
    keyPrefix = 'api:',
    cacheMethods = ['GET'],
    invalidateOnMutation = true
  } = options;

  return async (req, res, next) => {
    const { method, originalUrl, user } = req;

    if (!cacheMethods.includes(method)) {
      return next();
    }

    const cacheKey = generateCacheKey(keyPrefix, method, originalUrl, user);

    try {
      const cached = await cacheService.get(cacheKey);

      if (cached) {
        res.set('X-Cache', 'HIT');
        res.set('X-Cache-Key', cacheKey);
        return res.json(cached);
      }

      res.set('X-Cache', 'MISS');

      const originalJson = res.json.bind(res);
      res.json = function (body) {
        if (res.statusCode >= 200 && res.statusCode < 300 && body) {
          cacheService.set(cacheKey, body, ttl).catch(err => {
            console.error('Cache save error:', err);
          });
        }
        return originalJson(body);
      };

      if (invalidateOnMutation) {
        res.on('finish', () => {
          if (['POST', 'PUT', 'DELETE', 'PATCH'].includes(method) && res.statusCode >= 200 && res.statusCode < 300) {
            invalidateRelatedCaches(originalUrl);
          }
        });
      }

      next();
    } catch (error) {
      console.error('Cache middleware error:', error);
      next();
    }
  };
};

const generateCacheKey = (prefix, method, url, user) => {
  const userId = user?.id || 'anonymous';
  const cleanUrl = url.split('?')[0];
  return `${prefix}${method}:${userId}:${cleanUrl}`;
};

const invalidateRelatedCaches = async (url) => {
  try {
    const baseUrl = url.split('?')[0];
    const patterns = [
      `api:GET:*:${baseUrl}`,
      `api:GET:*:${baseUrl}/*`
    ];

    await cacheService.invalidatePatterns(patterns);
  } catch (error) {
    console.error('Cache invalidation error:', error);
  }
};

const cacheConfig = {
  auth: { ttl: 30 },
  users: { ttl: 60 },
  roles: { ttl: 120 },
  permissions: { ttl: 120 },
  logs: { ttl: 30 },
  config: { ttl: 300 }
};

module.exports = {
  cacheMiddleware,
  cacheConfig,
  generateCacheKey
};
