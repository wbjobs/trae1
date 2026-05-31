const PermissionService = require('../services/permissionService');

class PermissionController {
  static async getPermissions(req, res, next) {
    try {
      const { type, app } = req.query;
      const permissions = await PermissionService.getPermissions({ type, app });

      res.json({
        code: 200,
        message: '获取成功',
        data: { permissions }
      });
    } catch (error) {
      next(error);
    }
  }

  static async getPermissionTree(req, res, next) {
    try {
      const tree = await PermissionService.getPermissionTree();

      res.json({
        code: 200,
        message: '获取成功',
        data: tree
      });
    } catch (error) {
      next(error);
    }
  }

  static async getUserPermissions(req, res, next) {
    try {
      const permissions = await PermissionService.getUserPermissions(req.userId);

      res.json({
        code: 200,
        message: '获取成功',
        data: { permissions }
      });
    } catch (error) {
      next(error);
    }
  }

  static async getUserAppPermissions(req, res, next) {
    try {
      const appPermissions = await PermissionService.getUserAppPermissions(req.userId);

      res.json({
        code: 200,
        message: '获取成功',
        data: appPermissions
      });
    } catch (error) {
      next(error);
    }
  }

  static async checkPermission(req, res, next) {
    try {
      const { app, action } = req.body;
      const hasPermission = await PermissionService.checkUserPermission(req.userId, action);
      const hasAppAccess = app ? await PermissionService.checkAppAccess(req.userId, app) : true;

      res.json({
        code: 200,
        message: '校验成功',
        data: {
          hasPermission,
          hasAppAccess,
          allowed: hasPermission && hasAppAccess
        }
      });
    } catch (error) {
      next(error);
    }
  }

  static async createPermission(req, res, next) {
    try {
      const permission = await PermissionService.createPermission(req.body);

      res.status(201).json({
        code: 200,
        message: '创建成功',
        data: { permission: { id: permission.id, name: permission.name, code: permission.code } }
      });
    } catch (error) {
      next(error);
    }
  }

  static async updatePermission(req, res, next) {
    try {
      const { id } = req.params;
      const permission = await PermissionService.updatePermission(id, req.body);

      res.json({
        code: 200,
        message: '更新成功',
        data: { permission: { id: permission.id, name: permission.name } }
      });
    } catch (error) {
      next(error);
    }
  }

  static async deletePermission(req, res, next) {
    try {
      const { id } = req.params;
      const result = await PermissionService.deletePermission(id);

      res.json({
        code: 200,
        message: result.message
      });
    } catch (error) {
      next(error);
    }
  }
}

module.exports = PermissionController;
