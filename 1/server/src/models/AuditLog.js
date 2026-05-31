const { DataTypes, Model } = require('sequelize');
const { sequelize } = require('../config/database');

class AuditLog extends Model {}

AuditLog.init(
  {
    id: {
      type: DataTypes.BIGINT.UNSIGNED,
      primaryKey: true,
      autoIncrement: true
    },
    operationType: {
      type: DataTypes.STRING(20),
      allowNull: false,
      comment: '操作类型: create, update, delete, query'
    },
    tableName: {
      type: DataTypes.STRING(100),
      allowNull: false,
      comment: '数据表名'
    },
    recordId: {
      type: DataTypes.STRING(100),
      allowNull: true,
      comment: '记录ID'
    },
    oldValue: {
      type: DataTypes.TEXT,
      allowNull: true,
      comment: '修改前数据'
    },
    newValue: {
      type: DataTypes.TEXT,
      allowNull: true,
      comment: '修改后数据'
    },
    operator: {
      type: DataTypes.STRING(50),
      allowNull: true,
      comment: '操作人'
    },
    operatorId: {
      type: DataTypes.BIGINT.UNSIGNED,
      allowNull: true,
      comment: '操作人ID'
    },
    ip: {
      type: DataTypes.STRING(50),
      allowNull: true,
      comment: 'IP地址'
    },
    operationTime: {
      type: DataTypes.DATE,
      defaultValue: DataTypes.NOW,
      comment: '操作时间'
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
    modelName: 'AuditLog',
    tableName: 'sys_audit_logs'
  }
);

module.exports = AuditLog;
