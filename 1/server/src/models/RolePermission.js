const { DataTypes, Model } = require('sequelize');
const { sequelize } = require('../config/database');
const Role = require('./Role');
const Permission = require('./Permission');

class RolePermission extends Model {}

RolePermission.init(
  {
    id: {
      type: DataTypes.BIGINT.UNSIGNED,
      primaryKey: true,
      autoIncrement: true
    },
    roleId: {
      type: DataTypes.BIGINT.UNSIGNED,
      allowNull: false,
      references: {
        model: Role,
        key: 'id'
      },
      comment: '角色ID'
    },
    permissionId: {
      type: DataTypes.BIGINT.UNSIGNED,
      allowNull: false,
      references: {
        model: Permission,
        key: 'id'
      },
      comment: '权限ID'
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
    modelName: 'RolePermission',
    tableName: 'sys_role_permissions'
  }
);

module.exports = RolePermission;
