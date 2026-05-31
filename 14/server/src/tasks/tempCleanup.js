const fs = require('fs');
const path = require('path');
const Chunk = require('../models/Chunk');
const { getLogger } = require('../utils/logger');

const logger = getLogger('cleanup');

class TempCleanup {
  static async cleanTempFiles() {
    logger.info('开始清理临时文件...');

    const uploadDir = path.join(__dirname, '../../uploads');
    const chunksDir = path.join(uploadDir, 'chunks');
    const tempDir = path.join(uploadDir, 'temp');

    const now = Date.now();
    const maxAge = 24 * 60 * 60 * 1000;

    if (fs.existsSync(chunksDir)) {
      const chunkFolders = fs.readdirSync(chunksDir);
      
      for (const folder of chunkFolders) {
        const folderPath = path.join(chunksDir, folder);
        try {
          const stat = fs.statSync(folderPath);
          if (now - stat.mtime.getTime() > maxAge) {
            fs.rmSync(folderPath, { recursive: true, force: true });
            logger.info(`已删除过期分片目录: ${folder}`);
            
            await Chunk.destroy({ where: { fileId: folder } });
          }
        } catch (error) {
          logger.error(`清理分片目录失败: ${folder}`, error);
        }
      }
    }

    if (fs.existsSync(tempDir)) {
      const tempFiles = fs.readdirSync(tempDir);
      
      for (const file of tempFiles) {
        const filePath = path.join(tempDir, file);
        try {
          const stat = fs.statSync(filePath);
          if (now - stat.mtime.getTime() > maxAge) {
            fs.unlinkSync(filePath);
            logger.info(`已删除临时文件: ${file}`);
          }
        } catch (error) {
          logger.error(`清理临时文件失败: ${file}`, error);
        }
      }
    }

    logger.info('临时文件清理完成');
  }

  static start() {
    setInterval(() => {
      this.cleanTempFiles().catch(error => {
        logger.error('清理临时文件出错:', error);
      });
    }, 60 * 60 * 1000);

    logger.info('临时文件清理任务已启动');
  }
}

module.exports = TempCleanup;
