const userService = require('../services/userService');
const { generateToken } = require('../middleware/auth');

class AuthController {
  async register(ctx) {
    const { username, email, password, role } = ctx.request.body;
    
    try {
      const existingUser = await userService.findByEmail(email);
      if (existingUser) {
        ctx.status = 400;
        ctx.body = { message: '邮箱已被注册' };
        return;
      }
      
      const existingUsername = await userService.findByUsername(username);
      if (existingUsername) {
        ctx.status = 400;
        ctx.body = { message: '用户名已被使用' };
        return;
      }
      
      const user = await userService.createUser({
        username,
        email,
        password,
        role: role || 'viewer'
      });
      
      const token = generateToken(user);
      
      ctx.status = 201;
      ctx.body = {
        message: '注册成功',
        data: {
          user,
          token
        }
      };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }

  async login(ctx) {
    const { email, password } = ctx.request.body;
    
    try {
      const user = await userService.findByEmail(email);
      if (!user) {
        ctx.status = 401;
        ctx.body = { message: '邮箱或密码错误' };
        return;
      }
      
      const isValid = await userService.comparePassword(password, user.password);
      if (!isValid) {
        ctx.status = 401;
        ctx.body = { message: '邮箱或密码错误' };
        return;
      }
      
      if (user.status !== 'active') {
        ctx.status = 403;
        ctx.body = { message: '账号已被禁用' };
        return;
      }
      
      const ip = ctx.ip || ctx.request.ip;
      await userService.updateLoginInfo(user._id, ip);
      
      const token = generateToken(user);
      const userData = userService.sanitizeUser(user);
      
      ctx.body = {
        message: '登录成功',
        data: {
          user: userData,
          token
        }
      };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }

  async logout(ctx) {
    ctx.body = { message: '登出成功' };
  }

  async getCurrentUser(ctx) {
    try {
      const user = await userService.findById(ctx.state.user.id);
      
      if (!user) {
        ctx.status = 404;
        ctx.body = { message: '用户不存在' };
        return;
      }
      
      ctx.body = {
        data: user
      };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }

  async updateProfile(ctx) {
    const { username, email, avatar } = ctx.request.body;
    
    try {
      const updateData = {};
      if (username) updateData.username = username;
      if (email) updateData.email = email;
      if (avatar !== undefined) updateData.avatar = avatar;
      
      const user = await userService.updateUser(ctx.state.user.id, updateData);
      
      ctx.body = {
        message: '更新成功',
        data: user
      };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }

  async changePassword(ctx) {
    const { oldPassword, newPassword } = ctx.request.body;
    
    try {
      const user = await userService.findById(ctx.state.user.id, true);
      
      if (!user) {
        ctx.status = 404;
        ctx.body = { message: '用户不存在' };
        return;
      }
      
      const isValid = await userService.comparePassword(oldPassword, user.password);
      if (!isValid) {
        ctx.status = 400;
        ctx.body = { message: '原密码错误' };
        return;
      }
      
      await userService.updateUser(ctx.state.user.id, { password: newPassword });
      
      ctx.body = { message: '密码修改成功' };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }
}

module.exports = new AuthController();
