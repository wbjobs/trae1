const { Permission, Role, RolePermission } = require('../models');

const SUPER_ADMIN_CODE = 'super_admin';

const checkPermission = (requiredPermission) => {
  return async (req, res, next) => {
    try {
      const roles = req.roles || [];

      const isSuperAdmin = roles.some(role => role.code === SUPER_ADMIN_CODE);
      if (isSuperAdmin) {
        return next();
      }

      if (!requiredPermission) {
        return next();
      }

      const roleIds = roles.map(role => role.id);

      if (roleIds.length === 0) {
        return res.status(403).json({
          code: 403,
          message: '没有访问权限'
        });
      }

      const permission = await Permission.findOne({
        where: { code: requiredPermission, status: 1 }
      });

      if (!permission) {
        return next();
      }

      const rolePermission = await RolePermission.findOne({
        where: {
          roleId: roleIds,
          permissionId: permission.id
        }
      });

      if (!rolePermission) {
        return res.status(403).json({
          code: 403,
          message: '没有访问权限'
        });
      }

      next();
    } catch (error) {
      console.error('Permission check error:', error);
      return res.status(500).json({
        code: 500,
        message: '权限校验失败'
      });
    }
  };
};

const checkAppAccess = (appName) => {
  return async (req, res, next) => {
    try {
      const roles = req.roles || [];

      const isSuperAdmin = roles.some(role => role.code === SUPER_ADMIN_CODE);
      if (isSuperAdmin) {
        return next();
      }

      const roleIds = roles.map(role => role.id);

      if (roleIds.length === 0) {
        return res.status(403).json({
          code: 403,
          message: '没有访问该子应用的权限'
        });
      }

      const permissions = await Permission.findAll({
        where: { app: appName, status: 1 },
        attributes: ['id']
      });

      if (permissions.length === 0) {
        return next();
      }

      const permissionIds = permissions.map(p => p.id);

      const rolePermission = await RolePermission.findOne({
        where: {
          roleId: roleIds,
          permissionId: permissionIds
        }
      });

      if (!rolePermission) {
        return res.status(403).json({
          code: 403,
          message: '没有访问该子应用的权限'
        });
      }

      next();
    } catch (error) {
      console.error('App access check error:', error);
      return res.status(500).json({
        code: 500,
        message: '权限校验失败'
      });
    }
  };
};

const checkRoleLevel = (maxLevel = 5) => {
  return async (req, res, next) => {
    try {
      const roles = req.roles || [];

      const isSuperAdmin = roles.some(role => role.code === SUPER_ADMIN_CODE);
      if (isSuperAdmin) {
        return next();
      }

      const userMaxLevel = Math.min(...roles.map(role => role.level || 5));

      if (userMaxLevel > maxLevel) {
        return res.status(403).json({
          code: 403,
          message: '角色等级不足'
        });
      }

      next();
    } catch (error) {
      console.error('Role level check error:', error);
      return res.status(500).json({
        code: 500,
        message: '权限校验失败'
      });
    }
  };
};

module.exports = {
  checkPermission,
  checkAppAccess,
  checkRoleLevel,
  SUPER_ADMIN_CODE
};
