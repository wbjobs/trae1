const Router = require('koa-router');
const authController = require('../controllers/authController');
const { authMiddleware } = require('../middleware/auth');

const router = new Router({ prefix: '/api/auth' });

router.post('/register', authController.register);
router.post('/login', authController.login);
router.post('/logout', authMiddleware, authController.logout);
router.get('/me', authMiddleware, authController.getCurrentUser);
router.put('/profile', authMiddleware, authController.updateProfile);
router.put('/password', authMiddleware, authController.changePassword);

module.exports = router;
