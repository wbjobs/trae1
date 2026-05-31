const { SystemConfig } = require('../models');
const { BadRequestError, NotFoundError } = require('../middleware/errorHandler');

class SystemConfigService {
  static async getAllConfigs() {
    const configs = await SystemConfig.findAll({
      order: [['id', 'ASC']]
    });

    return configs.reduce((acc, config) => {
      acc[config.configKey] = this.parseValue(config.configValue, config.configType);
      return acc;
    }, {});
  }

  static async getConfig(key) {
    const config = await SystemConfig.findOne({ where: { configKey: key } });
    if (!config) {
      return null;
    }
    return this.parseValue(config.configValue, config.configType);
  }

  static async setConfig(key, value, type = 'string', description = '') {
    const config = await SystemConfig.findOne({ where: { configKey: key } });
    const stringValue = this.serializeValue(value, type);

    if (config) {
      await config.update({
        configValue: stringValue,
        configType: type,
        description
      });
      return config;
    }

    return await SystemConfig.create({
      configKey: key,
      configValue: stringValue,
      configType: type,
      description
    });
  }

  static async batchSetConfigs(configs) {
    const results = [];

    for (const config of configs) {
      const result = await this.setConfig(
        config.key,
        config.value,
        config.type,
        config.description
      );
      results.push(result);
    }

    return results;
  }

  static async deleteConfig(key) {
    const config = await SystemConfig.findOne({ where: { configKey: key } });
    if (!config) {
      throw new NotFoundError('配置不存在');
    }

    await config.destroy();
    return { message: '删除成功' };
  }

  static async getConfigList({ page = 1, pageSize = 20, keyword = '' } = {}) {
    const where = {};
    const { Op } = require('sequelize');

    if (keyword) {
      where[Op.or] = [
        { configKey: { [Op.like]: `%${keyword}%` } },
        { description: { [Op.like]: `%${keyword}%` } }
      ];
    }

    const { count, rows } = await SystemConfig.findAndCountAll({
      where,
      limit: pageSize,
      offset: (page - 1) * pageSize,
      order: [['id', 'ASC']]
    });

    return {
      configs: rows,
      total: count,
      page,
      pageSize
    };
  }

  static parseValue(value, type) {
    if (value === null || value === undefined) {
      return null;
    }

    switch (type) {
      case 'json':
        try {
          return JSON.parse(value);
        } catch (e) {
          return value;
        }
      case 'number':
        return Number(value);
      case 'boolean':
        return value === 'true' || value === '1';
      default:
        return value;
    }
  }

  static serializeValue(value, type) {
    switch (type) {
      case 'json':
        return JSON.stringify(value);
      case 'boolean':
        return value ? 'true' : 'false';
      default:
        return String(value);
    }
  }
}

module.exports = SystemConfigService;
