const { DataTypes, Model } = require('sequelize');
const { sequelize } = require('../config/database');

class Permission extends Model {}

Permission.init(
  {
    id: {
      type: DataTypes.BIGINT.UNSIGNED,
      primaryKey: true,
      autoIncrement: true
    },
    parentId: {
      type: DataTypes.BIGINT.UNSIGNED,
      allowNull: true,
      comment: '父级权限ID'
    },
    name: {
      type: DataTypes.STRING(50),
      allowNull: false,
      comment: '权限名称'
    },
    code: {
      type: DataTypes.STRING(100),
      allowNull: false,
      unique: true,
      comment: '权限编码'
    },
    type: {
      type: DataTypes.STRING(20),
      allowNull: false,
      comment: '权限类型: menu, button, api'
    },
    app: {
      type: DataTypes.STRING(50),
      allowNull: true,
      comment: '所属子应用'
    },
    path: {
      type: DataTypes.STRING(255),
      allowNull: true,
      comment: '路由路径'
    },
    method: {
      type: DataTypes.STRING(20),
      allowNull: true,
      comment: '请求方法'
    },
    icon: {
      type: DataTypes.STRING(50),
      allowNull: true,
      comment: '图标'
    },
    sort: {
      type: DataTypes.INTEGER,
      defaultValue: 0,
      comment: '排序'
    },
    description: {
      type: DataTypes.STRING(255),
      allowNull: true,
      comment: '权限描述'
    },
    status: {
      type: DataTypes.TINYINT,
      defaultValue: 1,
      comment: '状态: 1-启用, 0-禁用'
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
    modelName: 'Permission',
    tableName: 'sys_permissions'
  }
);

module.exports = Permission;
