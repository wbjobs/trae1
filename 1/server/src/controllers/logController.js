const LogService = require('../services/logService');

class LogController {
  static async getOperationLogs(req, res, next) {
    try {
      const { page, pageSize, module, username, action, status, startDate, endDate } = req.query;
      const result = await LogService.getOperationLogs({
        page: parseInt(page) || 1,
        pageSize: parseInt(pageSize) || 20,
        module,
        username,
        action,
        status,
        startDate,
        endDate
      });

      res.json({
        code: 200,
        message: '获取成功',
        data: result
      });
    } catch (error) {
      next(error);
    }
  }

  static async getOperationLogStats(req, res, next) {
    try {
      const stats = await LogService.getOperationLogStats();

      res.json({
        code: 200,
        message: '获取成功',
        data: stats
      });
    } catch (error) {
      next(error);
    }
  }

  static async getOperationLogById(req, res, next) {
    try {
      const { id } = req.params;
      const log = await LogService.getOperationLogById(id);

      res.json({
        code: 200,
        message: '获取成功',
        data: { log }
      });
    } catch (error) {
      next(error);
    }
  }

  static async reportLogs(req, res, next) {
    try {
      const { logs } = req.body;
      const result = await LogService.reportLogs(logs);

      res.json({
        code: 200,
        message: result.message,
        data: { count: result.count }
      });
    } catch (error) {
      next(error);
    }
  }

  static async getAuditLogs(req, res, next) {
    try {
      const { page, pageSize, operationType, tableName, operator, startDate, endDate } = req.query;
      const result = await LogService.getAuditLogs({
        page: parseInt(page) || 1,
        pageSize: parseInt(pageSize) || 20,
        operationType,
        tableName,
        operator,
        startDate,
        endDate
      });

      res.json({
        code: 200,
        message: '获取成功',
        data: result
      });
    } catch (error) {
      next(error);
    }
  }

  static async createAuditLog(req, res, next) {
    try {
      const auditLog = await LogService.createAuditLog(req.body);

      res.status(201).json({
        code: 200,
        message: '创建成功',
        data: { auditLog }
      });
    } catch (error) {
      next(error);
    }
  }

  static async cleanupLogs(req, res, next) {
    try {
      const { days } = req.body;
      const result = await LogService.cleanupOldLogs(days || 90);

      res.json({
        code: 200,
        message: result.message,
        data: {
          operationLogsDeleted: result.operationLogsDeleted,
          auditLogsDeleted: result.auditLogsDeleted
        }
      });
    } catch (error) {
      next(error);
    }
  }
}

module.exports = LogController;
