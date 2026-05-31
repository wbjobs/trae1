const { DataTypes, Model } = require('sequelize');
const sequelize = require('../config/database');

class Chunk extends Model {}

Chunk.init(
  {
    id: {
      type: DataTypes.UUID,
      primaryKey: true,
      defaultValue: DataTypes.UUIDV4
    },
    fileId: {
      type: DataTypes.STRING(100),
      allowNull: false,
      comment: '文件唯一标识(MD5)'
    },
    chunkIndex: {
      type: DataTypes.INTEGER,
      allowNull: false,
      comment: '分片索引'
    },
    chunkSize: {
      type: DataTypes.BIGINT,
      allowNull: false,
      comment: '分片大小'
    },
    chunkPath: {
      type: DataTypes.STRING(500),
      allowNull: false,
      comment: '分片存储路径'
    },
    status: {
      type: DataTypes.ENUM('pending', 'uploaded', 'merged'),
      defaultValue: 'uploaded',
      comment: '分片状态'
    },
    createdAt: {
      type: DataTypes.DATE,
      defaultValue: DataTypes.NOW
    }
  },
  {
    sequelize,
    modelName: 'Chunk',
    tableName: 'chunks',
    comment: '文件分片表',
    indexes: [
      {
        fields: ['fileId', 'chunkIndex'],
        unique: true
      }
    ]
  }
);

module.exports = Chunk;
