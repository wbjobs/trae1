const { User, Role, UserRole } = require('../models');
const { generateToken } = require('../middleware/auth');
const { BadRequestError, UnauthorizedError, NotFoundError } = require('../middleware/errorHandler');

class AuthService {
  static async login(username, password, ip) {
    if (!username || !password) {
      throw new BadRequestError('用户名和密码不能为空');
    }

    const user = await User.findOne({
      where: { username },
      include: [{
        model: Role,
        as: 'roles',
        through: { attributes: [] }
      }]
    });

    if (!user) {
      throw new UnauthorizedError('用户不存在');
    }

    if (user.status !== 1) {
      throw new UnauthorizedError('用户已被禁用');
    }

    const isValid = await user.validatePassword(password);
    if (!isValid) {
      throw new UnauthorizedError('密码错误');
    }

    const token = generateToken(user);

    await user.update({
      lastLoginAt: new Date(),
      lastLoginIp: ip
    });

    const userInfo = {
      id: user.id,
      username: user.username,
      email: user.email,
      phone: user.phone,
      avatar: user.avatar,
      roles: user.roles?.map(r => ({ id: r.id, name: r.name, code: r.code, level: r.level })) || []
    };

    return { token, user: userInfo };
  }

  static async getUserInfo(userId) {
    const user = await User.findByPk(userId, {
      attributes: { exclude: ['password'] },
      include: [{
        model: Role,
        as: 'roles',
        through: { attributes: [] }
      }]
    });

    if (!user) {
      throw new NotFoundError('用户不存在');
    }

    return {
      id: user.id,
      username: user.username,
      email: user.email,
      phone: user.phone,
      avatar: user.avatar,
      roles: user.roles?.map(r => ({ id: r.id, name: r.name, code: r.code, level: r.level })) || []
    };
  }

  static async changePassword(userId, oldPassword, newPassword) {
    const user = await User.findByPk(userId);

    if (!user) {
      throw new NotFoundError('用户不存在');
    }

    const isValid = await user.validatePassword(oldPassword);
    if (!isValid) {
      throw new BadRequestError('原密码错误');
    }

    await user.update({ password: newPassword });

    return { message: '密码修改成功' };
  }
}

module.exports = AuthService;
