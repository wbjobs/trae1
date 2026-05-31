const { Op } = require('sequelize');
const { OperationLog, AuditLog } = require('../models');
const { BadRequestError } = require('../middleware/errorHandler');

class LogService {
  static async getOperationLogs({
    page = 1,
    pageSize = 20,
    module,
    username,
    action,
    status,
    startDate,
    endDate
  } = {}) {
    const where = {};

    if (module) {
      where.module = module;
    }

    if (username) {
      where.username = { [Op.like]: `%${username}%` };
    }

    if (action) {
      where.action = action;
    }

    if (status) {
      where.status = status;
    }

    if (startDate && endDate) {
      where.createdAt = {
        [Op.between]: [new Date(startDate), new Date(endDate + ' 23:59:59')]
      };
    } else if (startDate) {
      where.createdAt = { [Op.gte]: new Date(startDate) };
    } else if (endDate) {
      where.createdAt = { [Op.lte]: new Date(endDate + ' 23:59:59') };
    }

    const { count, rows } = await OperationLog.findAndCountAll({
      where,
      limit: pageSize,
      offset: (page - 1) * pageSize,
      order: [['createdAt', 'DESC']]
    });

    return {
      logs: rows,
      total: count,
      page,
      pageSize
    };
  }

  static async getOperationLogStats() {
    const today = new Date();
    today.setHours(0, 0, 0, 0);

    const weekAgo = new Date(today);
    weekAgo.setDate(weekAgo.getDate() - 7);

    const monthAgo = new Date(today);
    monthAgo.setDate(monthAgo.getDate() - 30);

    const [todayCount, weekCount, monthCount, errorCount] = await Promise.all([
      OperationLog.count({ where: { createdAt: { [Op.gte]: today } } }),
      OperationLog.count({ where: { createdAt: { [Op.gte]: weekAgo } } }),
      OperationLog.count({ where: { createdAt: { [Op.gte]: monthAgo } } }),
      OperationLog.count({ where: { status: 'error' } })
    ]);

    return {
      todayCount,
      weekCount,
      monthCount,
      errorCount
    };
  }

  static async getOperationLogById(id) {
    const log = await OperationLog.findByPk(id);
    if (!log) {
      throw new BadRequestError('日志不存在');
    }
    return log;
  }

  static async reportLogs(logs) {
    if (!Array.isArray(logs) || logs.length === 0) {
      return { message: '无日志数据' };
    }

    const formattedLogs = logs.map(log => ({
      userId: log.user?.id || null,
      username: log.user?.username || null,
      module: log.module || 'frontend',
      action: log.action || log.type || 'log',
      description: log.detail ? JSON.stringify(log.detail).substring(0, 500) : '',
      method: log.method || 'GET',
      requestUrl: log.url || '',
      ip: log.ip || '',
      userAgent: log.userAgent || '',
      duration: log.duration || 0,
      status: log.level === 'error' ? 'error' : 'success',
      errorMessage: log.message || null,
      createdAt: log.timestamp || new Date()
    }));

    await OperationLog.bulkCreate(formattedLogs);

    return { message: '日志上报成功', count: formattedLogs.length };
  }

  static async getAuditLogs({
    page = 1,
    pageSize = 20,
    operationType,
    tableName,
    operator,
    startDate,
    endDate
  } = {}) {
    const where = {};

    if (operationType) {
      where.operationType = operationType;
    }

    if (tableName) {
      where.tableName = { [Op.like]: `%${tableName}%` };
    }

    if (operator) {
      where.operator = { [Op.like]: `%${operator}%` };
    }

    if (startDate && endDate) {
      where.operationTime = {
        [Op.between]: [new Date(startDate), new Date(endDate + ' 23:59:59')]
      };
    }

    const { count, rows } = await AuditLog.findAndCountAll({
      where,
      limit: pageSize,
      offset: (page - 1) * pageSize,
      order: [['operationTime', 'DESC']]
    });

    return {
      logs: rows,
      total: count,
      page,
      pageSize
    };
  }

  static async createAuditLog(auditData) {
    const auditLog = await AuditLog.create(auditData);
    return auditLog;
  }

  static async cleanupOldLogs(days = 90) {
    const cutoffDate = new Date();
    cutoffDate.setDate(cutoffDate.getDate() - days);

    const [operationDeleted, auditDeleted] = await Promise.all([
      OperationLog.destroy({ where: { createdAt: { [Op.lt]: cutoffDate } } }),
      AuditLog.destroy({ where: { operationTime: { [Op.lt]: cutoffDate } } })
    ]);

    return {
      message: '日志清理完成',
      operationLogsDeleted: operationDeleted,
      auditLogsDeleted: auditDeleted
    };
  }
}

module.exports = LogService;
