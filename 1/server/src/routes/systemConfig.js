const express = require('express');
const router = express.Router();
const SystemConfigController = require('../controllers/systemConfigController');
const { authMiddleware } = require('../middleware/auth');
const { checkPermission } = require('../middleware/permission');

router.get('/system-config', authMiddleware, checkPermission('config:view'), SystemConfigController.getAllConfigs);
router.get('/system-config/list', authMiddleware, checkPermission('config:view'), SystemConfigController.getConfigList);
router.get('/system-config/:key', authMiddleware, checkPermission('config:view'), SystemConfigController.getConfig);
router.post('/system-config', authMiddleware, checkPermission('config:update'), SystemConfigController.setConfig);
router.post('/system-config/batch', authMiddleware, checkPermission('config:update'), SystemConfigController.batchSetConfigs);
router.delete('/system-config/:key', authMiddleware, checkPermission('config:delete'), SystemConfigController.deleteConfig);

module.exports = router;
