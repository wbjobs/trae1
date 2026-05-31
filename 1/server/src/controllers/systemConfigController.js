const SystemConfigService = require('../services/systemConfigService');

class SystemConfigController {
  static async getAllConfigs(req, res, next) {
    try {
      const configs = await SystemConfigService.getAllConfigs();

      res.json({
        code: 200,
        message: '获取成功',
        data: { configs }
      });
    } catch (error) {
      next(error);
    }
  }

  static async getConfig(req, res, next) {
    try {
      const { key } = req.params;
      const value = await SystemConfigService.getConfig(key);

      if (value === null) {
        return res.status(404).json({
          code: 404,
          message: '配置不存在'
        });
      }

      res.json({
        code: 200,
        message: '获取成功',
        data: { key, value }
      });
    } catch (error) {
      next(error);
    }
  }

  static async setConfig(req, res, next) {
    try {
      const { key, value, type, description } = req.body;
      const config = await SystemConfigService.setConfig(key, value, type, description);

      res.json({
        code: 200,
        message: '保存成功',
        data: { config: { id: config.id, configKey: config.configKey } }
      });
    } catch (error) {
      next(error);
    }
  }

  static async batchSetConfigs(req, res, next) {
    try {
      const { configs } = req.body;
      const results = await SystemConfigService.batchSetConfigs(configs);

      res.json({
        code: 200,
        message: '批量保存成功',
        data: { count: results.length }
      });
    } catch (error) {
      next(error);
    }
  }

  static async deleteConfig(req, res, next) {
    try {
      const { key } = req.params;
      const result = await SystemConfigService.deleteConfig(key);

      res.json({
        code: 200,
        message: result.message
      });
    } catch (error) {
      next(error);
    }
  }

  static async getConfigList(req, res, next) {
    try {
      const { page, pageSize, keyword } = req.query;
      const result = await SystemConfigService.getConfigList({
        page: parseInt(page) || 1,
        pageSize: parseInt(pageSize) || 20,
        keyword
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
}

module.exports = SystemConfigController;
