const RoleService = require('../services/roleService');

class RoleController {
  static async getRoles(req, res, next) {
    try {
      const { page, pageSize, keyword, level, status } = req.query;
      const result = await RoleService.getRoles({
        page: parseInt(page) || 1,
        pageSize: parseInt(pageSize) || 50,
        keyword,
        level: level ? parseInt(level) : undefined,
        status: status !== undefined ? parseInt(status) : undefined
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

  static async getAllRoles(req, res, next) {
    try {
      const roles = await RoleService.getAllRoles();

      res.json({
        code: 200,
        message: '获取成功',
        data: { roles }
      });
    } catch (error) {
      next(error);
    }
  }

  static async getRoleById(req, res, next) {
    try {
      const { id } = req.params;
      const role = await RoleService.getRoleById(id);

      res.json({
        code: 200,
        message: '获取成功',
        data: { role }
      });
    } catch (error) {
      next(error);
    }
  }

  static async createRole(req, res, next) {
    try {
      const role = await RoleService.createRole(req.body);

      res.status(201).json({
        code: 200,
        message: '创建成功',
        data: { role: { id: role.id, name: role.name, code: role.code } }
      });
    } catch (error) {
      next(error);
    }
  }

  static async updateRole(req, res, next) {
    try {
      const { id } = req.params;
      const role = await RoleService.updateRole(id, req.body);

      res.json({
        code: 200,
        message: '更新成功',
        data: { role: { id: role.id, name: role.name } }
      });
    } catch (error) {
      next(error);
    }
  }

  static async deleteRole(req, res, next) {
    try {
      const { id } = req.params;
      const result = await RoleService.deleteRole(id);

      res.json({
        code: 200,
        message: result.message
      });
    } catch (error) {
      next(error);
    }
  }

  static async assignPermissions(req, res, next) {
    try {
      const { id } = req.params;
      const { permissionIds } = req.body;
      const result = await RoleService.assignPermissions(id, permissionIds);

      res.json({
        code: 200,
        message: result.message
      });
    } catch (error) {
      next(error);
    }
  }

  static async getRolePermissions(req, res, next) {
    try {
      const { id } = req.params;
      const permissions = await RoleService.getRolePermissions(id);

      res.json({
        code: 200,
        message: '获取成功',
        data: permissions
      });
    } catch (error) {
      next(error);
    }
  }
}

module.exports = RoleController;
