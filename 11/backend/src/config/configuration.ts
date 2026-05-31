export default () => ({
  port: parseInt(process.env.PORT, 10) || 3000,
  database: {
    host: process.env.DB_HOST || 'localhost',
    port: parseInt(process.env.DB_PORT, 10) || 3306,
    username: process.env.DB_USER || 'root',
    password: process.env.DB_PASS || 'root',
    database: process.env.DB_NAME || 'api_doc',
    synchronize: process.env.DB_SYNC !== 'false',
    logging: process.env.DB_LOG === 'true'
  },
  jwt: {
    secret: process.env.JWT_SECRET || 'api-doc-secret-key',
    expiresIn: process.env.JWT_EXPIRES || '7d'
  },
  admin: {
    username: process.env.ADMIN_USER || 'admin',
    password: process.env.ADMIN_PASS || 'admin123',
    email: process.env.ADMIN_EMAIL || 'admin@example.com'
  }
})
