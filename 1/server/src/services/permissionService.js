const { Op } = require('sequelize');
const { Permission, Role, RolePermission, UserRole, User } = require('../models');
const { BadRequestError, NotFoundError } = require('../middleware/errorHandler');
const cacheService = require('./cacheService');

class PermissionService {
  static async getPermissions({ type, app } = {}) {
    const cacheKey = `permissions:${type || 'all'}:${app || 'all'}`;

    const cached = await cacheService.get(cacheKey);
    if (cached) {
      return cached;
    }

    const where = { status: 1 };

    if (type) {
      where.type = type;
    }

    if (app) {
      where.app = app;
    }

    const permissions = await Permission.findAll({
      where,
      order: [['sort', 'ASC'], ['id', 'ASC']]
    });

    await cacheService.set(cacheKey, permissions, 300);

    return permissions;
  }

  static async getPermissionTree() {
    const cacheKey = 'permissions:tree';

    const cached = await cacheService.get(cacheKey);
    if (cached) {
      return cached;
    }

    const permissions = await Permission.findAll({
      where: { status: 1 },
      order: [['sort', 'ASC'], ['id', 'ASC']]
    });

    const tree = this.buildTree(permissions);

    await cacheService.set(cacheKey, tree, 300);

    return tree;
  }

  static buildTree(permissions, parentId = null) {
    return permissions
      .filter(p => (parentId === null ? p.parentId === null : p.parentId === parentId))
      .map(p => ({
        id: p.id,
        name: p.name,
        code: p.code,
        type: p.type,
        app: p.app,
        path: p.path,
        icon: p.icon,
        children: this.buildTree(permissions, p.id)
      }));
  }

  static async getUserPermissions(userId) {
    const cacheKey = `user:permissions:${userId}`;

    const cached = await cacheService.get(cacheKey);
    if (cached) {
      return cached;
    }

    const userRoles = await UserRole.findAll({
      where: { userId },
      include: [{
        model: Role,
        as: 'role',
        where: { status: 1 },
        attributes: ['id', 'code', 'level']
      }],
      attributes: ['roleId']
    });

    if (!userRoles || userRoles.length === 0) {
      await cacheService.set(cacheKey, [], 60);
      return [];
    }

    const roles = userRoles.map(ur => ur.role).filter(Boolean);
    const roleIds = roles.map(r => r.id);

    if (roles.some(r => r.code === 'super_admin')) {
      const allPermissions = await Permission.findAll({ where: { status: 1 } });
      await cacheService.set(cacheKey, allPermissions, 60);
      return allPermissions;
    }

    const rolePermissions = await RolePermission.findAll({
      where: { roleId: roleIds },
      attributes: ['permissionId']
    });

    const permissionIds = [...new Set(rolePermissions.map(rp => rp.permissionId))];

    if (permissionIds.length === 0) {
      await cacheService.set(cacheKey, [], 60);
      return [];
    }

    const permissions = await Permission.findAll({
      where: { id: permissionIds, status: 1 }
    });

    await cacheService.set(cacheKey, permissions, 60);

    return permissions;
  }

  static async getUserAppPermissions(userId) {
    const cacheKey = `user:app-permissions:${userId}`;

    const cached = await cacheService.get(cacheKey);
    if (cached) {
      return cached;
    }

    const permissions = await this.getUserPermissions(userId);

    const appPermissions = permissions
      .filter(p => p.app)
      .reduce((acc, p) => {
        if (!acc[p.app]) {
          acc[p.app] = [];
        }
        acc[p.app].push({
          id: p.id,
          code: p.code,
          name: p.name,
          type: p.type
        });
        return acc;
      }, {});

    await cacheService.set(cacheKey, appPermissions, 60);

    return appPermissions;
  }

  static async checkUserPermission(userId, permissionCode) {
    const cacheKey = `user:has-permission:${userId}:${permissionCode}`;

    const cached = await cacheService.get(cacheKey);
    if (cached !== null && cached !== undefined) {
      return cached;
    }

    const permissions = await this.getUserPermissions(userId);
    const hasPermission = permissions.some(p => p.code === permissionCode);

    await cacheService.set(cacheKey, hasPermission, 30);

    return hasPermission;
  }

  static async checkAppAccess(userId, appName) {
    const cacheKey = `user:has-app-access:${userId}:${appName}`;

    const cached = await cacheService.get(cacheKey);
    if (cached !== null && cached !== undefined) {
      return cached;
    }

    const permissions = await this.getUserPermissions(userId);
    const hasAccess = permissions.some(p => p.app === appName);

    await cacheService.set(cacheKey, hasAccess, 30);

    return hasAccess;
  }

  static async createPermission(permissionData) {
    const { code, name, type, app, path, method, icon, sort, parentId, description } = permissionData;

    const existingPermission = await Permission.findOne({ where: { code } });
    if (existingPermission) {
      throw new BadRequestError('权限编码已存在');
    }

    const permission = await Permission.create({
      code,
      name,
      type,
      app,
      path,
      method,
      icon,
      sort: sort || 0,
      parentId,
      description
    });

    await cacheService.invalidatePattern('permissions:*');

    return permission;
  }

  static async updatePermission(id, permissionData) {
    const permission = await Permission.findByPk(id);
    if (!permission) {
      throw new NotFoundError('权限不存在');
    }

    await permission.update(permissionData);

    await cacheService.invalidatePattern('permissions:*');

    return permission;
  }

  static async deletePermission(id) {
    const permission = await Permission.findByPk(id);
    if (!permission) {
      throw new NotFoundError('权限不存在');
    }

    const childCount = await Permission.count({ where: { parentId: id } });
    if (childCount > 0) {
      throw new BadRequestError('该权限下还有子权限，无法删除');
    }

    await RolePermission.destroy({ where: { permissionId: id } });
    await permission.destroy();

    await cacheService.invalidatePattern('permissions:*');
    await cacheService.invalidatePattern('user:*');

    return { message: '删除成功' };
  }

  static async invalidateUserCache(userId) {
    const patterns = [
      `user:permissions:${userId}`,
      `user:app-permissions:${userId}`,
      `user:has-permission:${userId}:*`,
      `user:has-app-access:${userId}:*`
    ];

    await cacheService.invalidatePatterns(patterns);
  }
}

module.exports = PermissionService;
