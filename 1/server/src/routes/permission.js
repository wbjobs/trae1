const express = require('express');
const router = express.Router();
const PermissionController = require('../controllers/permissionController');
const { authMiddleware } = require('../middleware/auth');
const { checkPermission } = require('../middleware/permission');

router.get('/permissions', authMiddleware, PermissionController.getUserPermissions);
router.get('/permissions/all', authMiddleware, checkPermission('permission:list'), PermissionController.getPermissions);
router.get('/permissions/tree', authMiddleware, PermissionController.getPermissionTree);
router.get('/permissions/app', authMiddleware, PermissionController.getUserAppPermissions);
router.post('/permissions/check', authMiddleware, PermissionController.checkPermission);
router.post('/permissions', authMiddleware, checkPermission('permission:create'), PermissionController.createPermission);
router.put('/permissions/:id', authMiddleware, checkPermission('permission:update'), PermissionController.updatePermission);
router.delete('/permissions/:id', authMiddleware, checkPermission('permission:delete'), PermissionController.deletePermission);

module.exports = router;
