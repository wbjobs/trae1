const fs = require('fs');
const path = require('path');
const File = require('../models/File');
const Share = require('../models/Share');
const { Op } = require('sequelize');
const cron = require('node-cron');

class CleanTask {
  static async cleanExpired() {
    try {
      console.log(`[${new Date().toISOString()}] 开始清理过期数据...`);

      const now = new Date();

      const expiredShares = await Share.findAll({
        where: {
          status: 'active',
          expireAt: {
            [Op.lt]: now
          }
        }
      });

      if (expiredShares.length > 0) {
        await Share.update(
          { status: 'expired' },
          {
            where: {
              id: {
                [Op.in]: expiredShares.map(s => s.id)
              }
            }
          }
        );
        console.log(`[清理任务] 已处理 ${expiredShares.length} 个过期分享`);
      } else {
        console.log('[清理任务] 没有过期的分享');
      }

      const expiredFiles = await File.findAll({
        where: {
          status: 'active',
          expireAt: {
            [Op.lt]: now
          }
        }
      });

      if (expiredFiles.length > 0) {
        for (const file of expiredFiles) {
          try {
            if (file.filePath && fs.existsSync(file.filePath)) {
              fs.unlinkSync(file.filePath);
              console.log(`[清理任务] 已删除文件: ${file.filePath}`);
            }
          } catch (err) {
            console.error(`[清理任务] 删除文件失败: ${file.filePath}`, err);
          }
          try {
            await file.update({ status: 'deleted' });
          } catch (err) {
            console.error(`[清理任务] 更新文件状态失败: ${file.id}`, err);
          }
        }
        console.log(`[清理任务] 已处理 ${expiredFiles.length} 个过期文件`);
      } else {
        console.log('[清理任务] 没有过期的文件');
      }

      console.log(`[${new Date().toISOString()}] 清理完成`);
    } catch (error) {
      console.error('[清理任务] 清理过期数据错误:', error);
    }
  }

  static start() {
    CleanTask.cleanExpired();

    cron.schedule('0 */1 * * *', () => {
      CleanTask.cleanExpired();
    });

    console.log('[清理任务] 定时清理任务已启动，每小时执行一次');
  }
}

module.exports = CleanTask;
