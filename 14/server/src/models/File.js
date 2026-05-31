const { DataTypes, Model } = require('sequelize');
const sequelize = require('../config/database');

class File extends Model {}

File.init(
  {
    id: {
      type: DataTypes.UUID,
      primaryKey: true,
      defaultValue: DataTypes.UUIDV4
    },
    originalName: {
      type: DataTypes.STRING(255),
      allowNull: false,
      comment: '原始文件名'
    },
    storedName: {
      type: DataTypes.STRING(255),
      allowNull: false,
      comment: '存储文件名'
    },
    filePath: {
      type: DataTypes.STRING(500),
      allowNull: false,
      comment: '文件存储路径'
    },
    fileSize: {
      type: DataTypes.BIGINT,
      allowNull: false,
      defaultValue: 0,
      comment: '文件大小(字节)'
    },
    mimeType: {
      type: DataTypes.STRING(100),
      allowNull: true,
      comment: 'MIME类型'
    },
    encryptionKey: {
      type: DataTypes.STRING(500),
      allowNull: false,
      comment: 'AES加密密钥'
    },
    encryptionIv: {
      type: DataTypes.STRING(100),
      allowNull: false,
      comment: 'AES加密IV'
    },
    algorithm: {
      type: DataTypes.STRING(50),
      allowNull: false,
      defaultValue: 'aes-256-cbc',
      comment: '加密算法'
    },
    ownerId: {
      type: DataTypes.STRING(100),
      allowNull: false,
      comment: '所有者ID'
    },
    ownerName: {
      type: DataTypes.STRING(100),
      allowNull: true,
      comment: '所有者名称'
    },
    status: {
      type: DataTypes.ENUM('active', 'expired', 'deleted'),
      defaultValue: 'active',
      comment: '文件状态'
    },
    expireAt: {
      type: DataTypes.DATE,
      allowNull: true,
      comment: '过期时间'
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
    modelName: 'File',
    tableName: 'files',
    comment: '文件表'
  }
);

module.exports = File;
