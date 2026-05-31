const express = require('express');
const router = express.Router();
const UserController = require('../controllers/userController');
const { authMiddleware } = require('../middleware/auth');
const { checkPermission, checkRoleLevel } = require('../middleware/permission');

router.get('/users', authMiddleware, checkPermission('user:list'), UserController.getUsers);
router.get('/users/:id', authMiddleware, checkPermission('user:view'), UserController.getUserById);
router.post('/users', authMiddleware, checkPermission('user:create'), checkRoleLevel(2), UserController.createUser);
router.put('/users/:id', authMiddleware, checkPermission('user:update'), UserController.updateUser);
router.delete('/users/:id', authMiddleware, checkPermission('user:delete'), checkRoleLevel(2), UserController.deleteUser);
router.patch('/users/:id/toggle-status', authMiddleware, checkPermission('user:update'), UserController.toggleUserStatus);

module.exports = router;
