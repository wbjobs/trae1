const Share = require('../models/Share');
const File = require('../models/File');
const CryptoUtil = require('../utils/crypto');
const { Op } = require('sequelize');

class ShareController {
  static async create(ctx) {
    try {
      const { fileId, permissionLevel, expireDays, password, accessLimit, allowedUsers, createdBy } = ctx.request.body;

      const file = await File.findByPk(fileId);
      if (!file || file.status !== 'active') {
        ctx.status = 404;
        ctx.body = { code: 404, message: '文件不存在或已失效' };
        return;
      }

      const shareCode = CryptoUtil.generateShareCode();
      
      const expireAt = expireDays
        ? new Date(Date.now() + parseInt(expireDays) * 24 * 60 * 60 * 1000)
        : null;

      const sharePassword = password ? CryptoUtil.hashPassword(password) : null;

      const share = await Share.create({
        fileId,
        shareCode,
        sharePassword,
        permissionLevel: permissionLevel || 'read',
        accessLimit: accessLimit || null,
        allowedUsers: allowedUsers || null,
        expireAt,
        createdBy: createdBy || 'anonymous'
      });

      ctx.status = 200;
      ctx.body = {
        code: 200,
        message: '创建分享成功',
        data: {
          shareId: share.id,
          shareCode: share.shareCode,
          permissionLevel: share.permissionLevel,
          expireAt: share.expireAt,
          hasPassword: !!sharePassword
        }
      };
    } catch (error) {
      console.error('创建分享错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '创建分享失败: ' + error.message };
    }
  }

  static async access(ctx) {
    try {
      const { shareCode } = ctx.params;
      const { password, userId } = ctx.request.body;

      const share = await Share.findOne({
        where: { shareCode },
        include: [{ model: File, as: 'file' }]
      });

      if (!share) {
        ctx.status = 404;
        ctx.body = { code: 404, message: '分享不存在' };
        return;
      }

      if (share.status !== 'active') {
        ctx.status = 400;
        ctx.body = { code: 400, message: '分享已失效' };
        return;
      }

      if (share.expireAt && new Date(share.expireAt) < new Date()) {
        await share.update({ status: 'expired' });
        ctx.status = 410;
        ctx.body = { code: 410, message: '分享已过期' };
        return;
      }

      if (share.accessLimit !== null && share.accessLimit !== undefined && share.accessCount >= share.accessLimit) {
        await share.update({ status: 'expired' });
        ctx.status = 403;
        ctx.body = { code: 403, message: '访问次数已用完' };
        return;
      }

      if (share.sharePassword) {
        if (!password || !CryptoUtil.verifyPassword(password, share.sharePassword)) {
          ctx.status = 401;
          ctx.body = { code: 401, message: '密码错误' };
          return;
        }
      }

      if (share.allowedUsers && Array.isArray(share.allowedUsers) && share.allowedUsers.length > 0) {
        if (!userId || !share.allowedUsers.includes(userId)) {
          ctx.status = 403;
          ctx.body = { code: 403, message: '无访问权限' };
          return;
        }
      }

      await share.increment('accessCount');

      const file = share.file;
      ctx.status = 200;
      ctx.body = {
        code: 200,
        data: {
          fileId: file.id,
          fileName: file.originalName,
          fileSize: file.fileSize,
          mimeType: file.mimeType,
          ownerName: file.ownerName,
          permissionLevel: share.permissionLevel,
          canDownload: ['download', 'admin'].includes(share.permissionLevel)
        }
      };
    } catch (error) {
      console.error('访问分享错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '访问失败' };
    }
  }

  static async list(ctx) {
    try {
      const { fileId, createdBy, page = 1, pageSize = 20 } = ctx.query;

      const where = {};
      if (fileId) where.fileId = fileId;
      if (createdBy) where.createdBy = createdBy;

      const { count, rows } = await Share.findAndCountAll({
        where,
        include: [{ model: File, as: 'file', attributes: ['id', 'originalName', 'fileSize'] }],
        order: [['createdAt', 'DESC']],
        limit: parseInt(pageSize),
        offset: (parseInt(page) - 1) * parseInt(pageSize)
      });

      ctx.status = 200;
      ctx.body = {
        code: 200,
        data: {
          list: rows.map(s => ({
            id: s.id,
            shareCode: s.shareCode,
            permissionLevel: s.permissionLevel,
            accessCount: s.accessCount,
            accessLimit: s.accessLimit,
            expireAt: s.expireAt,
            status: s.status,
            hasPassword: !!s.sharePassword,
            createdAt: s.createdAt,
            file: s.file ? {
              id: s.file.id,
              originalName: s.file.originalName,
              fileSize: s.file.fileSize
            } : null
          })),
          total: count,
          page: parseInt(page),
          pageSize: parseInt(pageSize)
        }
      };
    } catch (error) {
      console.error('获取分享列表错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '获取列表失败' };
    }
  }

  static async revoke(ctx) {
    try {
      const { id } = ctx.params;
      const { createdBy } = ctx.request.body;

      const share = await Share.findByPk(id);

      if (!share) {
        ctx.status = 404;
        ctx.body = { code: 404, message: '分享不存在' };
        return;
      }

      if (createdBy && share.createdBy !== createdBy) {
        ctx.status = 403;
        ctx.body = { code: 403, message: '无权限操作' };
        return;
      }

      await share.update({ status: 'revoked' });

      ctx.status = 200;
      ctx.body = { code: 200, message: '分享已撤销' };
    } catch (error) {
      console.error('撤销分享错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '撤销失败' };
    }
  }

  static async detail(ctx) {
    try {
      const { id } = ctx.params;

      const share = await Share.findByPk(id, {
        include: [{ model: File, as: 'file', attributes: ['id', 'originalName', 'fileSize', 'mimeType'] }]
      });

      if (!share) {
        ctx.status = 404;
        ctx.body = { code: 404, message: '分享不存在' };
        return;
      }

      ctx.status = 200;
      ctx.body = {
        code: 200,
        data: {
          id: share.id,
          shareCode: share.shareCode,
          permissionLevel: share.permissionLevel,
          accessCount: share.accessCount,
          accessLimit: share.accessLimit,
          expireAt: share.expireAt,
          status: share.status,
          hasPassword: !!share.sharePassword,
          allowedUsers: share.allowedUsers,
          createdAt: share.createdAt,
          file: share.file ? {
            id: share.file.id,
            originalName: share.file.originalName,
            fileSize: share.file.fileSize,
            mimeType: share.file.mimeType
          } : null
        }
      };
    } catch (error) {
      console.error('获取分享详情错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '获取详情失败' };
    }
  }
}

module.exports = ShareController;
