const jwt = require('jsonwebtoken');
const dotenv = require('dotenv');

dotenv.config();

const JWT_SECRET = process.env.JWT_SECRET;

const generateToken = (user) => {
  return jwt.sign(
    {
      id: user._id,
      username: user.username,
      email: user.email,
      role: user.role
    },
    JWT_SECRET,
    { expiresIn: process.env.JWT_EXPIRES_IN || '7d' }
  );
};

const verifyToken = (token) => {
  try {
    return jwt.verify(token, JWT_SECRET);
  } catch (error) {
    throw new Error('无效的认证令牌');
  }
};

const decodeToken = (token) => {
  return jwt.decode(token);
};

const authMiddleware = async (ctx, next) => {
  const authHeader = ctx.headers.authorization;
  
  if (!authHeader || !authHeader.startsWith('Bearer ')) {
    ctx.status = 401;
    ctx.body = { message: '缺少认证令牌' };
    return;
  }
  
  const token = authHeader.split(' ')[1];
  
  try {
    const decoded = verifyToken(token);
    ctx.state.user = decoded;
    await next();
  } catch (error) {
    ctx.status = 401;
    ctx.body = { message: '无效的认证令牌' };
  }
};

const optionalAuth = async (ctx, next) => {
  const authHeader = ctx.headers.authorization;
  
  if (authHeader && authHeader.startsWith('Bearer ')) {
    const token = authHeader.split(' ')[1];
    
    try {
      const decoded = verifyToken(token);
      ctx.state.user = decoded;
    } catch (error) {
      ctx.state.user = null;
    }
  } else {
    ctx.state.user = null;
  }
  
  await next();
};

const requireRole = (...roles) => {
  return async (ctx, next) => {
    if (!ctx.state.user) {
      ctx.status = 401;
      ctx.body = { message: '需要登录' };
      return;
    }
    
    if (!roles.includes(ctx.state.user.role)) {
      ctx.status = 403;
      ctx.body = { message: '权限不足' };
      return;
    }
    
    await next();
  };
};

module.exports = {
  generateToken,
  verifyToken,
  decodeToken,
  authMiddleware,
  optionalAuth,
  requireRole
};
