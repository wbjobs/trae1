const fs = require('fs');
const path = require('path');
const File = require('../models/File');
const CryptoUtil = require('../utils/crypto');
const WatermarkUtil = require('../utils/watermark');
const { Op, Sequelize } = require('sequelize');

class FileController {
  static async upload(ctx) {
    return new Promise((resolve, reject) => {
      try {
        const file = ctx.request.files.file;
        if (!file) {
          ctx.status = 400;
          ctx.body = { code: 400, message: '未找到上传文件' };
          return resolve();
        }

        const { expireDays, ownerId, ownerName } = ctx.request.body;
        const uploadDir = path.join(__dirname, '../../uploads');
        
        if (!fs.existsSync(uploadDir)) {
          fs.mkdirSync(uploadDir, { recursive: true });
        }

        const key = CryptoUtil.generateKey();
        const iv = CryptoUtil.generateIv();
        
        const storedName = `${Date.now()}_${Math.random().toString(36).substr(2, 9)}.enc`;
        const filePath = path.join(uploadDir, storedName);

        const sourcePath = file.filepath || file.path;
        const readStream = fs.createReadStream(sourcePath);
        const encryptStream = CryptoUtil.encryptStream(key, iv);
        const writeStream = fs.createWriteStream(filePath);

        let fileSize = 0;

        readStream.pipe(encryptStream).pipe(writeStream);

        readStream.on('data', (chunk) => {
          fileSize += chunk.length;
        });

        writeStream.on('finish', async () => {
          try {
            const expireAt = expireDays 
              ? new Date(Date.now() + parseInt(expireDays) * 24 * 60 * 60 * 1000) 
              : null;

            const fileRecord = await File.create({
              originalName: file.originalFilename || file.name,
              storedName,
              filePath,
              fileSize: fileSize || file.size,
              mimeType: file.mimetype || file.type,
              encryptionKey: key,
              encryptionIv: iv,
              ownerId: ownerId || 'anonymous',
              ownerName: ownerName || '匿名用户',
              expireAt
            });

            if (fs.existsSync(sourcePath) && sourcePath !== filePath) {
              try {
                fs.unlinkSync(sourcePath);
              } catch (e) {}
            }

            ctx.status = 200;
            ctx.body = {
              code: 200,
              message: '上传成功',
              data: {
                fileId: fileRecord.id,
                fileName: fileRecord.originalName,
                fileSize: fileRecord.fileSize,
                expireAt: fileRecord.expireAt
              }
            };
            resolve();
          } catch (error) {
            console.error('保存文件记录错误:', error);
            if (fs.existsSync(filePath)) {
              fs.unlinkSync(filePath);
            }
            ctx.status = 500;
            ctx.body = { code: 500, message: '上传失败: ' + error.message };
            resolve();
          }
        });

        writeStream.on('error', (error) => {
          console.error('写入文件错误:', error);
          if (fs.existsSync(filePath)) {
            fs.unlinkSync(filePath);
          }
          ctx.status = 500;
          ctx.body = { code: 500, message: '上传失败: ' + error.message };
          resolve();
        });

        readStream.on('error', (error) => {
          console.error('读取文件错误:', error);
          if (fs.existsSync(filePath)) {
            fs.unlinkSync(filePath);
          }
          ctx.status = 500;
          ctx.body = { code: 500, message: '上传失败: ' + error.message };
          resolve();
        });
      } catch (error) {
        console.error('上传文件错误:', error);
        ctx.status = 500;
        ctx.body = { code: 500, message: '上传失败: ' + error.message };
        resolve();
      }
    });
  }

  static async list(ctx) {
    try {
      const { ownerId, page = 1, pageSize = 20, keyword } = ctx.query;
      
      const where = { status: 'active' };
      if (ownerId) {
        where.ownerId = ownerId;
      }
      if (keyword) {
        where.originalName = { [Op.like]: `%${keyword}%` };
      }

      const { count, rows } = await File.findAndCountAll({
        where,
        order: [['createdAt', 'DESC']],
        limit: parseInt(pageSize),
        offset: (parseInt(page) - 1) * parseInt(pageSize)
      });

      ctx.status = 200;
      ctx.body = {
        code: 200,
        data: {
          list: rows.map(f => ({
            id: f.id,
            originalName: f.originalName,
            fileSize: f.fileSize,
            mimeType: f.mimeType,
            ownerName: f.ownerName,
            expireAt: f.expireAt,
            createdAt: f.createdAt
          })),
          total: count,
          page: parseInt(page),
          pageSize: parseInt(pageSize)
        }
      };
    } catch (error) {
      console.error('获取文件列表错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '获取列表失败' };
    }
  }

  static async detail(ctx) {
    try {
      const { id } = ctx.params;
      const file = await File.findByPk(id);

      if (!file || file.status !== 'active') {
        ctx.status = 404;
        ctx.body = { code: 404, message: '文件不存在或已失效' };
        return;
      }

      if (file.expireAt && new Date(file.expireAt) < new Date()) {
        ctx.status = 410;
        ctx.body = { code: 410, message: '文件已过期' };
        return;
      }

      ctx.status = 200;
      ctx.body = {
        code: 200,
        data: {
          id: file.id,
          originalName: file.originalName,
          fileSize: file.fileSize,
          mimeType: file.mimeType,
          ownerName: file.ownerName,
          expireAt: file.expireAt,
          createdAt: file.createdAt
        }
      };
    } catch (error) {
      console.error('获取文件详情错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '获取详情失败' };
    }
  }

  static async download(ctx) {
    try {
      const { id } = ctx.params;
      const file = await File.findByPk(id);

      if (!file || file.status !== 'active') {
        ctx.status = 404;
        ctx.body = { code: 404, message: '文件不存在或已失效' };
        return;
      }

      if (file.expireAt && new Date(file.expireAt) < new Date()) {
        ctx.status = 410;
        ctx.body = { code: 410, message: '文件已过期' };
        return;
      }

      if (!fs.existsSync(file.filePath)) {
        ctx.status = 404;
        ctx.body = { code: 404, message: '文件不存在' };
        return;
      }

      const readStream = fs.createReadStream(file.filePath);
      const decryptStream = CryptoUtil.decryptStream(
        file.encryptionKey,
        file.encryptionIv
      );

      ctx.set('Content-Type', file.mimeType || 'application/octet-stream');
      ctx.set('Content-Disposition', `attachment; filename="${encodeURIComponent(file.originalName)}"`);
      ctx.set('Content-Length', file.fileSize);
      
      ctx.body = readStream.pipe(decryptStream);
    } catch (error) {
      console.error('下载文件错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '下载失败: ' + error.message };
    }
  }

  static async delete(ctx) {
    try {
      const { id } = ctx.params;
      const { ownerId } = ctx.request.body;

      const file = await File.findByPk(id);

      if (!file) {
        ctx.status = 404;
        ctx.body = { code: 404, message: '文件不存在' };
        return;
      }

      if (ownerId && file.ownerId !== ownerId) {
        ctx.status = 403;
        ctx.body = { code: 403, message: '无权限删除' };
        return;
      }

      if (fs.existsSync(file.filePath)) {
        fs.unlinkSync(file.filePath);
      }

      await file.update({ status: 'deleted' });

      ctx.status = 200;
      ctx.body = { code: 200, message: '删除成功' };
    } catch (error) {
      console.error('删除文件错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '删除失败' };
    }
  }

  static async preview(ctx) {
    try {
      const { id } = ctx.params;
      const { watermark } = ctx.query;
      const file = await File.findByPk(id);

      if (!file || file.status !== 'active') {
        ctx.status = 404;
        ctx.body = { code: 404, message: '文件不存在或已失效' };
        return;
      }

      if (file.expireAt && new Date(file.expireAt) < new Date()) {
        ctx.status = 410;
        ctx.body = { code: 410, message: '文件已过期' };
        return;
      }

      if (!fs.existsSync(file.filePath)) {
        ctx.status = 404;
        ctx.body = { code: 404, message: '文件不存在' };
        return;
      }

      const previewTypes = ['image/', 'video/', 'audio/', 'text/', 'application/pdf'];
      const canPreview = previewTypes.some(type => file.mimeType?.startsWith(type));

      if (!canPreview) {
        ctx.status = 400;
        ctx.body = { code: 400, message: '该文件类型不支持预览' };
        return;
      }

      const encryptedBuffer = fs.readFileSync(file.filePath);
      const decryptedBuffer = CryptoUtil.decrypt(
        encryptedBuffer,
        file.encryptionKey,
        file.encryptionIv
      );

      let outputBuffer = decryptedBuffer;

      if (watermark && WatermarkUtil.isImageFile(file.mimeType)) {
        outputBuffer = await WatermarkUtil.addTileWatermark(decryptedBuffer, {
          text: '机密文件',
          fontSize: 30,
          opacity: 0.2
        });
      }

      ctx.set('Content-Type', file.mimeType || 'application/octet-stream');
      ctx.set('Content-Disposition', `inline; filename="${encodeURIComponent(file.originalName)}"`);
      
      ctx.body = outputBuffer;
    } catch (error) {
      console.error('预览文件错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '预览失败: ' + error.message };
    }
  }

  static async stats(ctx) {
    try {
      const totalFiles = await File.count({ where: { status: 'active' } });
      
      const totalSize = await File.sum('fileSize', { where: { status: 'active' } }) || 0;
      
      const fileTypeStats = await File.findAll({
        where: { status: 'active' },
        attributes: [
          [Sequelize.fn('LOWER', Sequelize.fn('SUBSTRING_INDEX', Sequelize.col('mime_type'), '/', 1)), 'fileType'],
          [Sequelize.fn('COUNT', '*'), 'count'],
          [Sequelize.fn('SUM', Sequelize.col('file_size')), 'totalSize']
        ],
        group: ['fileType'],
        raw: true
      });

      const recentUploads = await File.findAll({
        where: { status: 'active' },
        attributes: ['id', 'originalName', 'fileSize', 'mimeType', 'createdAt'],
        order: [['createdAt', 'DESC']],
        limit: 10
      });

      const expiresToday = await File.count({
        where: {
          status: 'active',
          expireAt: {
            [Op.lt]: new Date(Date.now() + 24 * 60 * 60 * 1000)
          }
        }
      });

      ctx.status = 200;
      ctx.body = {
        code: 200,
        data: {
          totalFiles,
          totalSize,
          fileTypeStats: fileTypeStats.map(item => ({
            type: item.fileType || 'other',
            count: item.count,
            totalSize: item.totalSize || 0
          })),
          recentUploads: recentUploads.map(f => ({
            id: f.id,
            originalName: f.originalName,
            fileSize: f.fileSize,
            mimeType: f.mimeType,
            createdAt: f.createdAt
          })),
          expiresToday
        }
      };
    } catch (error) {
      console.error('获取统计数据错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '获取统计失败' };
    }
  }
}

module.exports = FileController;
