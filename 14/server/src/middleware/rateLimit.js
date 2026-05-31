class RateLimiter {
  constructor(options = {}) {
    this.windowMs = options.windowMs || 60 * 1000;
    this.maxRequests = options.maxRequests || 100;
    this.clients = new Map();
  }

  isAllowed(clientId) {
    const now = Date.now();
    const clientData = this.clients.get(clientId);

    if (!clientData) {
      this.clients.set(clientId, {
        count: 1,
        resetTime: now + this.windowMs
      });
      return { allowed: true, remaining: this.maxRequests - 1 };
    }

    if (now > clientData.resetTime) {
      this.clients.set(clientId, {
        count: 1,
        resetTime: now + this.windowMs
      });
      return { allowed: true, remaining: this.maxRequests - 1 };
    }

    if (clientData.count >= this.maxRequests) {
      return {
        allowed: false,
        remaining: 0,
        retryAfter: Math.ceil((clientData.resetTime - now) / 1000)
      };
    }

    clientData.count++;
    return { allowed: true, remaining: this.maxRequests - clientData.count };
  }

  reset(clientId) {
    this.clients.delete(clientId);
  }

  cleanup() {
    const now = Date.now();
    for (const [clientId, data] of this.clients.entries()) {
      if (now > data.resetTime) {
        this.clients.delete(clientId);
      }
    }
  }
}

const uploadLimiter = new RateLimiter({
  windowMs: 60 * 1000,
  maxRequests: 10
});

const apiLimiter = new RateLimiter({
  windowMs: 60 * 1000,
  maxRequests: 100
});

const shareAccessLimiter = new RateLimiter({
  windowMs: 60 * 1000,
  maxRequests: 30
});

setInterval(() => {
  uploadLimiter.cleanup();
  apiLimiter.cleanup();
  shareAccessLimiter.cleanup();
}, 5 * 60 * 1000);

module.exports = {
  RateLimiter,
  uploadLimiter,
  apiLimiter,
  shareAccessLimiter,
  
  rateLimitMiddleware(limiter) {
    return async (ctx, next) => {
      const clientId = ctx.ip || ctx.request.ip || 'unknown';
      const result = limiter.isAllowed(clientId);

      if (!result.allowed) {
        ctx.status = 429;
        ctx.set('Retry-After', result.retryAfter);
        ctx.body = {
          code: 429,
          message: '请求过于频繁，请稍后再试'
        };
        return;
      }

      ctx.set('X-RateLimit-Remaining', result.remaining);
      await next();
    };
  }
};
