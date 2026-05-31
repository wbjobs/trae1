const { Sequelize } = require('sequelize');

const sequelize = new Sequelize({
  host: process.env.DB_HOST || 'localhost',
  port: process.env.DB_PORT || 3306,
  database: process.env.DB_NAME || 'secure_share',
  username: process.env.DB_USER || 'root',
  password: process.env.DB_PASS || 'root',
  dialect: 'mysql',
  timezone: '+08:00',
  logging: false,
  pool: {
    max: 10,
    min: 0,
    acquire: 30000,
    idle: 10000
  }
});

module.exports = sequelize;
