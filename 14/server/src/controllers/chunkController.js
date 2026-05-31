const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const Chunk = require('../models/Chunk');
const File = require('../models/File');
const CryptoUtil = require('../utils/crypto');

class ChunkController {
  static async verifyChunk(ctx) {
    try {
      const { fileId, chunkIndex, chunkSize } = ctx.query;

      if (!fileId) {
        ctx.status = 400;
        ctx.body = { code: 400, message: '缺少文件标识' };
        return;
      }

      const chunk = await Chunk.findOne({
        where: { fileId, chunkIndex: parseInt(chunkIndex) }
      });

      if (chunk && chunk.status === 'uploaded') {
        ctx.status = 200;
        ctx.body = {
          code: 200,
          data: {
            uploaded: true,
            chunkIndex: parseInt(chunkIndex)
          }
        };
      } else {
        ctx.status = 200;
        ctx.body = {
          code: 200,
          data: {
            uploaded: false,
            chunkIndex: parseInt(chunkIndex)
          }
        };
      }
    } catch (error) {
      console.error('验证分片错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '验证失败' };
    }
  }

  static async uploadChunk(ctx) {
    return new Promise((resolve, reject) => {
      try {
        const { fileId, chunkIndex, totalChunks, fileName, fileSize } = ctx.request.body;
        const chunkFile = ctx.request.files.chunk;

        if (!fileId || !chunkFile) {
          ctx.status = 400;
          ctx.body = { code: 400, message: '缺少必要参数' };
          return resolve();
        }

        const chunkDir = path.join(__dirname, '../../uploads', 'chunks', fileId);
        
        if (!fs.existsSync(chunkDir)) {
          fs.mkdirSync(chunkDir, { recursive: true });
        }

        const chunkPath = path.join(chunkDir, `chunk_${parseInt(chunkIndex)}`);
        const sourcePath = chunkFile.filepath || chunkFile.path;

        const readStream = fs.createReadStream(sourcePath);
        const writeStream = fs.createWriteStream(chunkPath);

        readStream.pipe(writeStream);

        writeStream.on('finish', async () => {
          try {
            const stat = fs.statSync(chunkPath);
            
            await Chunk.upsert({
              fileId,
              chunkIndex: parseInt(chunkIndex),
              chunkSize: stat.size,
              chunkPath,
              status: 'uploaded'
            });

            if (sourcePath !== chunkPath && fs.existsSync(sourcePath)) {
              try {
                fs.unlinkSync(sourcePath);
              } catch (e) {}
            }

            const uploadedChunks = await Chunk.count({
              where: { fileId, status: 'uploaded' }
            });

            ctx.status = 200;
            ctx.body = {
              code: 200,
              message: '分片上传成功',
              data: {
                fileId,
                chunkIndex: parseInt(chunkIndex),
                uploadedChunks,
                totalChunks: parseInt(totalChunks)
              }
            };
            resolve();
          } catch (error) {
            console.error('保存分片记录错误:', error);
            if (fs.existsSync(chunkPath)) {
              fs.unlinkSync(chunkPath);
            }
            ctx.status = 500;
            ctx.body = { code: 500, message: '分片上传失败' };
            resolve();
          }
        });

        writeStream.on('error', (error) => {
          console.error('写入分片错误:', error);
          if (fs.existsSync(chunkPath)) {
            fs.unlinkSync(chunkPath);
          }
          ctx.status = 500;
          ctx.body = { code: 500, message: '分片上传失败' };
          resolve();
        });
      } catch (error) {
        console.error('上传分片错误:', error);
        ctx.status = 500;
        ctx.body = { code: 500, message: '分片上传失败' };
        resolve();
      }
    });
  }

  static async mergeChunks(ctx) {
    return new Promise((resolve, reject) => {
      try {
        const { fileId, fileName, mimeType, expireDays, ownerId, ownerName } = ctx.request.body;

        if (!fileId) {
          ctx.status = 400;
          ctx.body = { code: 400, message: '缺少文件标识' };
          return resolve();
        }

        const chunkDir = path.join(__dirname, '../../uploads', 'chunks', fileId);
        
        if (!fs.existsSync(chunkDir)) {
          ctx.status = 400;
          ctx.body = { code: 400, message: '分片目录不存在' };
          return resolve();
        }

        const chunks = fs.readdirSync(chunkDir)
          .filter(f => f.startsWith('chunk_'))
          .sort((a, b) => {
            const indexA = parseInt(a.replace('chunk_', ''));
            const indexB = parseInt(b.replace('chunk_', ''));
            return indexA - indexB;
          });

        if (chunks.length === 0) {
          ctx.status = 400;
          ctx.body = { code: 400, message: '没有找到分片文件' };
          return resolve();
        }

        const key = CryptoUtil.generateKey();
        const iv = CryptoUtil.generateIv();

        const uploadDir = path.join(__dirname, '../../uploads');
        const storedName = `${Date.now()}_${Math.random().toString(36).substr(2, 9)}.enc`;
        const filePath = path.join(uploadDir, storedName);

        const encryptStream = CryptoUtil.encryptStream(key, iv);
        const writeStream = fs.createWriteStream(filePath);

        let mergedSize = 0;
        let currentChunk = 0;

        const mergeNextChunk = () => {
          if (currentChunk >= chunks.length) {
            encryptStream.end();
            return;
          }

          const chunkPath = path.join(chunkDir, chunks[currentChunk]);
          const readStream = fs.createReadStream(chunkPath);

          readStream.on('data', (chunk) => {
            mergedSize += chunk.length;
          });

          readStream.on('end', () => {
            currentChunk++;
            mergeNextChunk();
          });

          readStream.on('error', (error) => {
            console.error('读取分片错误:', error);
            cleanup();
            ctx.status = 500;
            ctx.body = { code: 500, message: '合并分片失败' };
            resolve();
          });

          if (currentChunk === 0) {
            readStream.pipe(encryptStream).pipe(writeStream, { end: false });
          } else {
            readStream.pipe(encryptStream, { end: false });
          }
        };

        const cleanup = () => {
          if (fs.existsSync(chunkDir)) {
            fs.rmSync(chunkDir, { recursive: true, force: true });
          }
          Chunk.destroy({ where: { fileId } }).catch(console.error);
        };

        writeStream.on('finish', async () => {
          try {
            const expireAt = expireDays 
              ? new Date(Date.now() + parseInt(expireDays) * 24 * 60 * 60 * 1000) 
              : null;

            const fileRecord = await File.create({
              originalName: fileName || 'unknown',
              storedName,
              filePath,
              fileSize: mergedSize,
              mimeType: mimeType || 'application/octet-stream',
              encryptionKey: key,
              encryptionIv: iv,
              ownerId: ownerId || 'anonymous',
              ownerName: ownerName || '匿名用户',
              expireAt
            });

            cleanup();

            ctx.status = 200;
            ctx.body = {
              code: 200,
              message: '合并成功',
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
            cleanup();
            if (fs.existsSync(filePath)) {
              fs.unlinkSync(filePath);
            }
            ctx.status = 500;
            ctx.body = { code: 500, message: '合并失败' };
            resolve();
          }
        });

        writeStream.on('error', (error) => {
          console.error('写入文件错误:', error);
          cleanup();
          if (fs.existsSync(filePath)) {
            fs.unlinkSync(filePath);
          }
          ctx.status = 500;
          ctx.body = { code: 500, message: '合并失败' };
          resolve();
        });

        encryptStream.on('error', (error) => {
          console.error('加密错误:', error);
          cleanup();
          if (fs.existsSync(filePath)) {
            fs.unlinkSync(filePath);
          }
          ctx.status = 500;
          ctx.body = { code: 500, message: '合并失败' };
          resolve();
        });

        mergeNextChunk();
      } catch (error) {
        console.error('合并分片错误:', error);
        ctx.status = 500;
        ctx.body = { code: 500, message: '合并失败' };
        resolve();
      }
    });
  }

  static async getUploadedChunks(ctx) {
    try {
      const { fileId } = ctx.params;

      if (!fileId) {
        ctx.status = 400;
        ctx.body = { code: 400, message: '缺少文件标识' };
        return;
      }

      const chunks = await Chunk.findAll({
        where: { fileId, status: 'uploaded' },
        attributes: ['chunkIndex', 'chunkSize'],
        order: [['chunkIndex', 'ASC']]
      });

      ctx.status = 200;
      ctx.body = {
        code: 200,
        data: {
          fileId,
          uploadedChunks: chunks.map(c => c.chunkIndex),
          totalUploaded: chunks.length
        }
      };
    } catch (error) {
      console.error('获取已上传分片错误:', error);
      ctx.status = 500;
      ctx.body = { code: 500, message: '获取失败' };
    }
  }
}

module.exports = ChunkController;
