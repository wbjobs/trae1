const { DataTypes, Model } = require('sequelize');
const { sequelize } = require('../config/database');

class OperationLog extends Model {}

OperationLog.init(
  {
    id: {
      type: DataTypes.BIGINT.UNSIGNED,
      primaryKey: true,
      autoIncrement: true
    },
    userId: {
      type: DataTypes.BIGINT.UNSIGNED,
      allowNull: true,
      comment: '用户ID'
    },
    username: {
      type: DataTypes.STRING(50),
      allowNull: true,
      comment: '用户名'
    },
    module: {
      type: DataTypes.STRING(50),
      allowNull: true,
      comment: '模块'
    },
    action: {
      type: DataTypes.STRING(50),
      allowNull: true,
      comment: '操作类型'
    },
    description: {
      type: DataTypes.STRING(500),
      allowNull: true,
      comment: '操作描述'
    },
    method: {
      type: DataTypes.STRING(20),
      allowNull: true,
      comment: '请求方法'
    },
    requestUrl: {
      type: DataTypes.STRING(500),
      allowNull: true,
      comment: '请求URL'
    },
    requestParams: {
      type: DataTypes.TEXT,
      allowNull: true,
      comment: '请求参数'
    },
    responseData: {
      type: DataTypes.TEXT,
      allowNull: true,
      comment: '响应数据'
    },
    ip: {
      type: DataTypes.STRING(50),
      allowNull: true,
      comment: 'IP地址'
    },
    userAgent: {
      type: DataTypes.STRING(500),
      allowNull: true,
      comment: '用户代理'
    },
    duration: {
      type: DataTypes.INTEGER,
      allowNull: true,
      comment: '耗时(毫秒)'
    },
    status: {
      type: DataTypes.STRING(20),
      defaultValue: 'success',
      comment: '状态: success, error'
    },
    errorMessage: {
      type: DataTypes.TEXT,
      allowNull: true,
      comment: '错误信息'
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
    modelName: 'OperationLog',
    tableName: 'sys_operation_logs'
  }
);

module.exports = OperationLog;
