const AuthService = require('../services/authService');
const { operationLogger } = require('../middleware/operationLogger');

class AuthController {
  static async login(req, res, next) {
    try {
      const { username, password } = req.body;
      const ip = req.headers['x-forwarded-for'] || req.connection.remoteAddress;

      const result = await AuthService.login(username, password, ip);

      res.json({
        code: 200,
        message: '登录成功',
        data: result
      });
    } catch (error) {
      next(error);
    }
  }

  static async logout(req, res, next) {
    try {
      res.json({
        code: 200,
        message: '退出成功'
      });
    } catch (error) {
      next(error);
    }
  }

  static async getUserInfo(req, res, next) {
    try {
      const userInfo = await AuthService.getUserInfo(req.userId);

      res.json({
        code: 200,
        message: '获取成功',
        data: { user: userInfo }
      });
    } catch (error) {
      next(error);
    }
  }

  static async changePassword(req, res, next) {
    try {
      const { oldPassword, newPassword } = req.body;
      const result = await AuthService.changePassword(req.userId, oldPassword, newPassword);

      res.json({
        code: 200,
        message: result.message
      });
    } catch (error) {
      next(error);
    }
  }
}

module.exports = AuthController;
