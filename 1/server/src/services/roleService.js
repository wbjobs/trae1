const { Op } = require('sequelize');
const { Role, Permission, RolePermission, UserRole, sequelize } = require('../models');
const { BadRequestError, NotFoundError } = require('../middleware/errorHandler');

class RoleService {
  static async getRoles({ page = 1, pageSize = 50, keyword = '', level, status } = {}) {
    const where = {};

    if (keyword) {
      where[Op.or] = [
        { name: { [Op.like]: `%${keyword}%` } },
        { code: { [Op.like]: `%${keyword}%` } }
      ];
    }

    if (level) {
      where.level = level;
    }

    if (status !== undefined) {
      where.status = status;
    }

    const { count, rows } = await Role.findAndCountAll({
      where,
      include: [{
        model: Permission,
        as: 'permissions',
        through: { attributes: [] }
      }],
      limit: pageSize,
      offset: (page - 1) * pageSize,
      order: [['level', 'ASC'], ['createdAt', 'DESC']]
    });

    const rolesWithUserCount = await Promise.all(
      rows.map(async (role) => {
        const userCount = await UserRole.count({ where: { roleId: role.id } });
        return { ...role.toJSON(), userCount };
      })
    );

    return {
      roles: rolesWithUserCount,
      total: count,
      page,
      pageSize
    };
  }

  static async getAllRoles() {
    const roles = await Role.findAll({
      where: { status: 1 },
      order: [['level', 'ASC']]
    });
    return roles;
  }

  static async getRoleById(id) {
    const role = await Role.findByPk(id, {
      include: [{
        model: Permission,
        as: 'permissions',
        through: { attributes: [] }
      }]
    });

    if (!role) {
      throw new NotFoundError('角色不存在');
    }

    return role;
  }

  static async createRole(roleData) {
    const { name, code, level, description, permissionIds } = roleData;

    const existingRole = await Role.findOne({ where: { code } });
    if (existingRole) {
      throw new BadRequestError('角色编码已存在');
    }

    const transaction = await sequelize.transaction();

    try {
      const role = await Role.create({
        name,
        code,
        level: level || 3,
        description
      }, { transaction });

      if (permissionIds && permissionIds.length > 0) {
        const rolePermissions = permissionIds.map(permissionId => ({
          roleId: role.id,
          permissionId
        }));
        await RolePermission.bulkCreate(rolePermissions, { transaction });
      }

      await transaction.commit();
      return role;
    } catch (error) {
      await transaction.rollback();
      throw error;
    }
  }

  static async updateRole(id, roleData) {
    const role = await Role.findByPk(id);
    if (!role) {
      throw new NotFoundError('角色不存在');
    }

    const { permissionIds, ...updateData } = roleData;

    const transaction = await sequelize.transaction();

    try {
      await role.update(updateData, { transaction });

      if (permissionIds) {
        await RolePermission.destroy({ where: { roleId: id }, transaction });
        const rolePermissions = permissionIds.map(permissionId => ({
          roleId: id,
          permissionId
        }));
        await RolePermission.bulkCreate(rolePermissions, { transaction });
      }

      await transaction.commit();
      return role;
    } catch (error) {
      await transaction.rollback();
      throw error;
    }
  }

  static async deleteRole(id) {
    const role = await Role.findByPk(id);
    if (!role) {
      throw new NotFoundError('角色不存在');
    }

    const userCount = await UserRole.count({ where: { roleId: id } });
    if (userCount > 0) {
      throw new BadRequestError('该角色下还有用户，无法删除');
    }

    const transaction = await sequelize.transaction();

    try {
      await RolePermission.destroy({ where: { roleId: id }, transaction });
      await role.destroy({ transaction });
      await transaction.commit();
      return { message: '删除成功' };
    } catch (error) {
      await transaction.rollback();
      throw error;
    }
  }

  static async assignPermissions(id, permissionIds) {
    const role = await Role.findByPk(id);
    if (!role) {
      throw new NotFoundError('角色不存在');
    }

    const transaction = await sequelize.transaction();

    try {
      await RolePermission.destroy({ where: { roleId: id }, transaction });

      if (permissionIds && permissionIds.length > 0) {
        const rolePermissions = permissionIds.map(permissionId => ({
          roleId: id,
          permissionId
        }));
        await RolePermission.bulkCreate(rolePermissions, { transaction });
      }

      await transaction.commit();
      return { message: '权限分配成功' };
    } catch (error) {
      await transaction.rollback();
      throw error;
    }
  }

  static async getRolePermissions(id) {
    const role = await Role.findByPk(id, {
      include: [{
        model: Permission,
        as: 'permissions',
        through: { attributes: [] }
      }]
    });

    if (!role) {
      throw new NotFoundError('角色不存在');
    }

    return role.permissions || [];
  }
}

module.exports = RoleService;
