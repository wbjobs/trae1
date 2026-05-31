const express = require('express');
const router = express.Router();
const LogController = require('../controllers/logController');
const { authMiddleware, optionalAuthMiddleware } = require('../middleware/auth');
const { checkPermission } = require('../middleware/permission');

router.get('/logs/operation', authMiddleware, checkPermission('log:view'), LogController.getOperationLogs);
router.get('/logs/operation/stats', authMiddleware, LogController.getOperationLogStats);
router.get('/logs/operation/:id', authMiddleware, checkPermission('log:view'), LogController.getOperationLogById);
router.post('/logs/report', optionalAuthMiddleware, LogController.reportLogs);
router.get('/logs/audit', authMiddleware, checkPermission('audit:view'), LogController.getAuditLogs);
router.post('/logs/audit', authMiddleware, LogController.createAuditLog);
router.post('/logs/cleanup', authMiddleware, checkPermission('log:delete'), LogController.cleanupLogs);

module.exports = router;
