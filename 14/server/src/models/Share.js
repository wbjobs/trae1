const { DataTypes, Model } = require('sequelize');
const sequelize = require('../config/database');

class Share extends Model {}

Share.init(
  {
    id: {
      type: DataTypes.UUID,
      primaryKey: true,
      defaultValue: DataTypes.UUIDV4
    },
    fileId: {
      type: DataTypes.UUID,
      allowNull: false,
      comment: '文件ID',
      references: {
        model: 'files',
        key: 'id'
      }
    },
    shareCode: {
      type: DataTypes.STRING(20),
      allowNull: false,
      unique: true,
      comment: '分享码'
    },
    sharePassword: {
      type: DataTypes.STRING(100),
      allowNull: true,
      comment: '访问密码'
    },
    permissionLevel: {
      type: DataTypes.ENUM('read', 'download', 'admin'),
      defaultValue: 'read',
      comment: '权限级别: read-仅查看, download-可下载, admin-完全控制'
    },
    accessLimit: {
      type: DataTypes.INTEGER,
      allowNull: true,
      comment: '访问次数限制, null表示无限制'
    },
    accessCount: {
      type: DataTypes.INTEGER,
      defaultValue: 0,
      comment: '已访问次数'
    },
    allowedUsers: {
      type: DataTypes.JSON,
      allowNull: true,
      comment: '允许访问的用户列表, null表示公开'
    },
    expireAt: {
      type: DataTypes.DATE,
      allowNull: true,
      comment: '分享过期时间'
    },
    status: {
      type: DataTypes.ENUM('active', 'expired', 'revoked'),
      defaultValue: 'active',
      comment: '分享状态'
    },
    createdBy: {
      type: DataTypes.STRING(100),
      allowNull: false,
      comment: '创建者ID'
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
    modelName: 'Share',
    tableName: 'shares',
    comment: '分享记录表'
  }
);

module.exports = Share;
