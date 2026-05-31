const express = require('express');
const router = express.Router();
const RoleController = require('../controllers/roleController');
const { authMiddleware } = require('../middleware/auth');
const { checkPermission, checkRoleLevel } = require('../middleware/permission');

router.get('/roles', authMiddleware, checkPermission('role:list'), RoleController.getRoles);
router.get('/roles/all', authMiddleware, RoleController.getAllRoles);
router.get('/roles/:id', authMiddleware, checkPermission('role:view'), RoleController.getRoleById);
router.post('/roles', authMiddleware, checkPermission('role:create'), checkRoleLevel(1), RoleController.createRole);
router.put('/roles/:id', authMiddleware, checkPermission('role:update'), RoleController.updateRole);
router.delete('/roles/:id', authMiddleware, checkPermission('role:delete'), checkRoleLevel(1), RoleController.deleteRole);
router.post('/roles/:id/permissions', authMiddleware, checkPermission('role:assign'), RoleController.assignPermissions);
router.get('/roles/:id/permissions', authMiddleware, checkPermission('role:view'), RoleController.getRolePermissions);

module.exports = router;
