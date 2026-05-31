const Router = require('koa-router');
const permissionController = require('../controllers/permissionController');
const { authMiddleware, requireRole } = require('../middleware/auth');

const router = new Router({ prefix: '/api/permissions' });

router.get('/document/:documentId', authMiddleware, permissionController.getDocumentPermissions);
router.post('/grant', authMiddleware, requireRole('admin'), permissionController.grantPermission);
router.post('/revoke', authMiddleware, requireRole('admin'), permissionController.revokePermission);
router.post('/check', authMiddleware, permissionController.checkPermission);
router.get('/user/:userId', authMiddleware, permissionController.getUserPermissions);
router.get('/resource/:resourceType/:resourceId', authMiddleware, permissionController.getResourcePermissions);
router.post('/user/:userId/batch-grant', authMiddleware, requireRole('admin'), permissionController.batchGrantPermissions);
router.post('/user/:userId/batch-revoke', authMiddleware, requireRole('admin'), permissionController.batchRevokePermissions);

module.exports = router;
