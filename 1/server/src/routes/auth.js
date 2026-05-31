const express = require('express');
const router = express.Router();
const AuthController = require('../controllers/authController');
const { authMiddleware, optionalAuthMiddleware } = require('../middleware/auth');
const { operationLogger } = require('../middleware/operationLogger');

router.post('/auth/login', operationLogger({ module: 'auth', action: 'login' }), AuthController.login);
router.post('/auth/logout', authMiddleware, AuthController.logout);
router.get('/auth/userinfo', authMiddleware, AuthController.getUserInfo);
router.post('/auth/change-password', authMiddleware, AuthController.changePassword);

module.exports = router;
