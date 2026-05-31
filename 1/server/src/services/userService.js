const { Op } = require('sequelize');
const { User, Role, UserRole, sequelize } = require('../models');
const { BadRequestError, NotFoundError } = require('../middleware/errorHandler');

class UserService {
  static async getUsers({ page = 1, pageSize = 20, keyword = '', status } = {}) {
    const where = {};

    if (keyword) {
      where[Op.or] = [
        { username: { [Op.like]: `%${keyword}%` } },
        { email: { [Op.like]: `%${keyword}%` } }
      ];
    }

    if (status !== undefined) {
      where.status = status;
    }

    const { count, rows } = await User.findAndCountAll({
      where,
      attributes: { exclude: ['password'] },
      include: [{
        model: Role,
        as: 'roles',
        through: { attributes: [] }
      }],
      limit: pageSize,
      offset: (page - 1) * pageSize,
      order: [['createdAt', 'DESC']]
    });

    return {
      users: rows,
      total: count,
      page,
      pageSize
    };
  }

  static async createUser(userData) {
    const { username, password, email, phone, roleIds } = userData;

    const existingUser = await User.findOne({ where: { username } });
    if (existingUser) {
      throw new BadRequestError('用户名已存在');
    }

    const transaction = await sequelize.transaction();

    try {
      const user = await User.create({
        username,
        password,
        email,
        phone
      }, { transaction });

      if (roleIds && roleIds.length > 0) {
        const userRoles = roleIds.map(roleId => ({
          userId: user.id,
          roleId
        }));
        await UserRole.bulkCreate(userRoles, { transaction });
      }

      await transaction.commit();
      return user;
    } catch (error) {
      await transaction.rollback();
      throw error;
    }
  }

  static async updateUser(id, userData) {
    const user = await User.findByPk(id);
    if (!user) {
      throw new NotFoundError('用户不存在');
    }

    const { roleIds, ...updateData } = userData;

    const transaction = await sequelize.transaction();

    try {
      await user.update(updateData, { transaction });

      if (roleIds) {
        await UserRole.destroy({ where: { userId: id }, transaction });
        const userRoles = roleIds.map(roleId => ({
          userId: id,
          roleId
        }));
        await UserRole.bulkCreate(userRoles, { transaction });
      }

      await transaction.commit();
      return user;
    } catch (error) {
      await transaction.rollback();
      throw error;
    }
  }

  static async deleteUser(id) {
    const user = await User.findByPk(id);
    if (!user) {
      throw new NotFoundError('用户不存在');
    }

    await UserRole.destroy({ where: { userId: id } });
    await user.destroy();
    return { message: '删除成功' };
  }

  static async toggleUserStatus(id) {
    const user = await User.findByPk(id);
    if (!user) {
      throw new NotFoundError('用户不存在');
    }

    await user.update({ status: user.status === 1 ? 0 : 1 });
    return { message: '状态更新成功' };
  }
}

module.exports = UserService;
