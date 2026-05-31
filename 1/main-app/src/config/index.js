const BASE_URL = process.env.NODE_ENV === 'production'
  ? 'http://api.example.com'
  : 'http://localhost:3000';

const API_CONFIG = {
  BASE_URL,
  TIMEOUT: 10000,
  TOKEN_KEY: 'access_token',
  USER_INFO_KEY: 'user_info'
};

const PERMISSION_CONFIG = {
  SUPER_ADMIN_ROLE: 'super_admin',
  PUBLIC_ROUTES: ['/login', '/403', '/404'],
  DEFAULT_ROUTE: '/dashboard'
};

const LOG_CONFIG = {
  ENABLE_REPORT: true,
  REPORT_URL: `${BASE_URL}/api/logs/report`,
  BATCH_SIZE: 10,
  FLUSH_INTERVAL: 5000
};

module.exports = {
  API_CONFIG,
  PERMISSION_CONFIG,
  LOG_CONFIG
};
