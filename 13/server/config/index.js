module.exports = {
  port: process.env.PORT || 3000,
  db: {
    host: process.env.DB_HOST || '127.0.0.1',
    port: Number(process.env.DB_PORT) || 3306,
    user: process.env.DB_USER || 'root',
    password: process.env.DB_PASSWORD || '123456',
    database: process.env.DB_NAME || 'log_mask',
    waitForConnections: true,
    connectionLimit: 10,
    queueLimit: 0
  },
  upload: {
    maxFileSize: 20 * 1024 * 1024,
    dest: 'uploads'
  },
  logFormats: ['plain', 'json', 'csv', 'nginx', 'syslog', 'apache']
};
