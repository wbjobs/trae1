require('dotenv').config();

const config = {
  env: process.env.NODE_ENV || 'development',
  port: process.env.PORT || 3000,

  database: {
    host: process.env.DB_HOST || 'localhost',
    port: process.env.DB_PORT || 3306,
    name: process.env.DB_NAME || 'micro_frontend',
    user: process.env.DB_USER || 'root',
    password: process.env.DB_PASSWORD || '123456'
  },

  jwt: {
    secret: process.env.JWT_SECRET || 'micro_frontend_jwt_secret_key_2024',
    expiresIn: process.env.JWT_EXPIRES_IN || '24h'
  },

  logging: {
    level: process.env.LOG_LEVEL || 'debug',
    dir: process.env.LOG_DIR || './logs'
  },

  cors: {
    origin: '*',
    methods: ['GET', 'POST', 'PUT', 'DELETE', 'OPTIONS'],
    allowedHeaders: ['Content-Type', 'Authorization']
  }
};

module.exports = config;
