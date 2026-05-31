const { DataTypes, Model } = require('sequelize');
const { sequelize } = require('../config/database');

class SystemConfig extends Model {}

SystemConfig.init(
  {
    id: {
      type: DataTypes.BIGINT.UNSIGNED,
      primaryKey: true,
      autoIncrement: true
    },
    configKey: {
      type: DataTypes.STRING(100),
      allowNull: false,
      unique: true,
      comment: '配置键'
    },
    configValue: {
      type: DataTypes.TEXT,
      allowNull: true,
      comment: '配置值'
    },
    configType: {
      type: DataTypes.STRING(20),
      defaultValue: 'string',
      comment: '值类型: string, number, boolean, json'
    },
    description: {
      type: DataTypes.STRING(255),
      allowNull: true,
      comment: '配置描述'
    },
    createdAt: {
      type: DataTypes.DATE,
      defaultValue: DataTypes.NOW
    },
    updatedAt: {
      type: DataTypes.DATE,
      defaultValue: DataTypes.NOW
    }
  },
  {
    sequelize,
    modelName: 'SystemConfig',
    tableName: 'sys_system_config'
  }
);

module.exports = SystemConfig;
