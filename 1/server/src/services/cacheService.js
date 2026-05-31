class CacheService {
  constructor(options = {}) {
    this.cache = new Map();
    this.defaultTTL = options.defaultTTL || 300;
    this.maxSize = options.maxSize || 1000;
    this.stats = {
      hits: 0,
      misses: 0,
      sets: 0,
      evictions: 0
    };

    this.startCleanup();
  }

  async get(key) {
    const entry = this.cache.get(key);

    if (!entry) {
      this.stats.misses++;
      return null;
    }

    if (this.isExpired(entry)) {
      this.cache.delete(key);
      this.stats.misses++;
      return null;
    }

    this.stats.hits++;
    entry.lastAccess = Date.now();
    entry.accessCount++;

    return entry.value;
  }

  async set(key, value, ttl = this.defaultTTL) {
    if (this.cache.size >= this.maxSize) {
      this.evictLRU();
    }

    this.cache.set(key, {
      value,
      expiresAt: Date.now() + (ttl * 1000),
      createdAt: Date.now(),
      lastAccess: Date.now(),
      accessCount: 0
    });

    this.stats.sets++;
  }

  async delete(key) {
    this.cache.delete(key);
  }

  async invalidatePattern(pattern) {
    const regex = this.patternToRegex(pattern);
    let count = 0;

    for (const key of this.cache.keys()) {
      if (regex.test(key)) {
        this.cache.delete(key);
        count++;
      }
    }

    return count;
  }

  async invalidatePatterns(patterns) {
    let totalCount = 0;

    for (const pattern of patterns) {
      const count = await this.invalidatePattern(pattern);
      totalCount += count;
    }

    return totalCount;
  }

  async clear() {
    this.cache.clear();
    this.resetStats();
  }

  isExpired(entry) {
    return Date.now() > entry.expiresAt;
  }

  patternToRegex(pattern) {
    const escaped = pattern.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const regexStr = escaped.replace(/\\\*/g, '.*');
    return new RegExp(`^${regexStr}$`);
  }

  evictLRU() {
    let oldestKey = null;
    let oldestTime = Infinity;

    for (const [key, entry] of this.cache.entries()) {
      if (entry.lastAccess < oldestTime) {
        oldestTime = entry.lastAccess;
        oldestKey = key;
      }
    }

    if (oldestKey) {
      this.cache.delete(oldestKey);
      this.stats.evictions++;
    }
  }

  startCleanup() {
    const cleanupInterval = 60000;

    setInterval(() => {
      const now = Date.now();
      for (const [key, entry] of this.cache.entries()) {
        if (now > entry.expiresAt) {
          this.cache.delete(key);
        }
      }
    }, cleanupInterval);
  }

  resetStats() {
    this.stats = {
      hits: 0,
      misses: 0,
      sets: 0,
      evictions: 0
    };
  }

  getStats() {
    return {
      ...this.stats,
      size: this.cache.size,
      hitRate: this.stats.hits / (this.stats.hits + this.stats.misses) || 0
    };
  }

  getKeys() {
    return Array.from(this.cache.keys());
  }
}

const cacheService = new CacheService({
  defaultTTL: 300,
  maxSize: 1000
});

module.exports = cacheService;
