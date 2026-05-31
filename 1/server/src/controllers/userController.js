const UserService = require('../services/userService');

class UserController {
  static async getUsers(req, res, next) {
    try {
      const { page, pageSize, keyword, status } = req.query;
      const result = await UserService.getUsers({
        page: parseInt(page) || 1,
        pageSize: parseInt(pageSize) || 20,
        keyword,
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

  static async getUserById(req, res, next) {
    try {
      const { id } = req.params;
      const { User } = require('../models');
      const user = await User.findByPk(id, {
        attributes: { exclude: ['password'] }
      });

      if (!user) {
        return res.status(404).json({
          code: 404,
          message: '用户不存在'
        });
      }

      res.json({
        code: 200,
        message: '获取成功',
        data: { user }
      });
    } catch (error) {
      next(error);
    }
  }

  static async createUser(req, res, next) {
    try {
      const user = await UserService.createUser(req.body);

      res.status(201).json({
        code: 200,
        message: '创建成功',
        data: { user: { id: user.id, username: user.username } }
      });
    } catch (error) {
      next(error);
    }
  }

  static async updateUser(req, res, next) {
    try {
      const { id } = req.params;
      const user = await UserService.updateUser(id, req.body);

      res.json({
        code: 200,
        message: '更新成功',
        data: { user: { id: user.id, username: user.username } }
      });
    } catch (error) {
      next(error);
    }
  }

  static async deleteUser(req, res, next) {
    try {
      const { id } = req.params;
      const result = await UserService.deleteUser(id);

      res.json({
        code: 200,
        message: result.message
      });
    } catch (error) {
      next(error);
    }
  }

  static async toggleUserStatus(req, res, next) {
    try {
      const { id } = req.params;
      const result = await UserService.toggleUserStatus(id);

      res.json({
        code: 200,
        message: result.message
      });
    } catch (error) {
      next(error);
    }
  }
}

module.exports = UserController;
