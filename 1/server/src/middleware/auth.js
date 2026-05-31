const jwt = require('jsonwebtoken');
const config = require('../config');
const { User, Role } = require('../models');

const generateToken = (user) => {
  return jwt.sign(
    {
      id: user.id,
      username: user.username
    },
    config.jwt.secret,
    {
      expiresIn: config.jwt.expiresIn
    }
  );
};

const verifyToken = (token) => {
  try {
    return jwt.verify(token, config.jwt.secret);
  } catch (error) {
    return null;
  }
};

const authMiddleware = async (req, res, next) => {
  const authHeader = req.headers.authorization;

  if (!authHeader || !authHeader.startsWith('Bearer ')) {
    return res.status(401).json({
      code: 401,
      message: '未提供认证令牌'
    });
  }

  const token = authHeader.split(' ')[1];

  try {
    const decoded = verifyToken(token);

    if (!decoded) {
      return res.status(401).json({
        code: 401,
        message: '令牌无效或已过期'
      });
    }

    const user = await User.findByPk(decoded.id, {
      attributes: { exclude: ['password'] },
      include: [{
        model: Role,
        as: 'roles',
        through: { attributes: [] },
        where: { status: 1 },
        required: false
      }]
    });

    if (!user || user.status !== 1) {
      return res.status(401).json({
        code: 401,
        message: '用户不存在或已被禁用'
      });
    }

    req.user = user;
    req.userId = user.id;
    req.username = user.username;
    req.roles = user.roles || [];

    next();
  } catch (error) {
    console.error('Auth middleware error:', error);
    return res.status(500).json({
      code: 500,
      message: '认证失败',
      error: process.env.NODE_ENV === 'development' ? error.message : undefined
    });
  }
};

const optionalAuthMiddleware = async (req, res, next) => {
  const authHeader = req.headers.authorization;

  if (authHeader && authHeader.startsWith('Bearer ')) {
    const token = authHeader.split(' ')[1];
    const decoded = verifyToken(token);

    if (decoded) {
      try {
        const user = await User.findByPk(decoded.id, {
          attributes: { exclude: ['password'] },
          include: [{
            model: Role,
            as: 'roles',
            through: { attributes: [] },
            where: { status: 1 },
            required: false
          }]
        });

        if (user && user.status === 1) {
          req.user = user;
          req.userId = user.id;
          req.username = user.username;
          req.roles = user.roles || [];
        }
      } catch (error) {
        console.error('Optional auth middleware error:', error);
      }
    }
  }

  next();
};

module.exports = {
  generateToken,
  verifyToken,
  authMiddleware,
  optionalAuthMiddleware
};
