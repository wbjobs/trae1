const permissionService = require('../services/permissionService');

class PermissionController {
  async getDocumentPermissions(ctx) {
    const { documentId } = ctx.params;
    const userId = ctx.state.user.id;
    
    try {
      const canManage = await permissionService.canManageDocument(documentId, userId);
      
      if (!canManage) {
        ctx.status = 403;
        ctx.body = { message: '无管理权限' };
        return;
      }
      
      const permissions = await permissionService.getDocumentPermissions(documentId);
      
      if (!permissions) {
        ctx.status = 404;
        ctx.body = { message: '文档不存在' };
        return;
      }
      
      ctx.body = {
        data: permissions
      };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }

  async grantPermission(ctx) {
    const { userId, resourceType, resourceId, action, expiresAt } = ctx.request.body;
    
    try {
      const permission = await permissionService.grantPermission(
        userId,
        resourceType,
        resourceId,
        action,
        expiresAt
      );
      
      ctx.status = 201;
      ctx.body = {
        message: '权限授予成功',
        data: permission
      };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }

  async revokePermission(ctx) {
    const { userId, resourceType, resourceId, action } = ctx.request.body;
    
    try {
      await permissionService.revokePermission(userId, resourceType, resourceId, action);
      
      ctx.body = { message: '权限撤销成功' };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }

  async checkPermission(ctx) {
    const { userId, resourceType, resourceId, action } = ctx.request.body;
    
    try {
      const hasPermission = await permissionService.checkPermission(
        userId,
        resourceType,
        resourceId,
        action
      );
      
      ctx.body = {
        data: { hasPermission }
      };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }

  async getUserPermissions(ctx) {
    const { userId } = ctx.params;
    const { resourceType, resourceId } = ctx.query;
    
    try {
      const permissions = await permissionService.getUserPermissions(
        userId,
        resourceType,
        resourceId
      );
      
      ctx.body = {
        data: permissions
      };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }

  async getResourcePermissions(ctx) {
    const { resourceType, resourceId } = ctx.params;
    
    try {
      const permissions = await permissionService.getResourcePermissions(
        resourceType,
        resourceId
      );
      
      ctx.body = {
        data: permissions
      };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }

  async batchGrantPermissions(ctx) {
    const { userId } = ctx.params;
    const { permissions } = ctx.request.body;
    
    try {
      const result = await permissionService.batchGrantPermissions(userId, permissions);
      
      ctx.status = 201;
      ctx.body = {
        message: '批量授权成功',
        data: result
      };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }

  async batchRevokePermissions(ctx) {
    const { userId } = ctx.params;
    const { resourceType, resourceIds } = ctx.request.body;
    
    try {
      await permissionService.batchRevokePermissions(userId, resourceType, resourceIds);
      
      ctx.body = { message: '批量撤销成功' };
    } catch (error) {
      ctx.status = 400;
      ctx.body = { message: error.message };
    }
  }
}

module.exports = new PermissionController();
