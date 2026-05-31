const Permission = require('../models/Permission');
const Document = require('../models/Document');

class PermissionService {
  async grantPermission(userId, resourceType, resourceId, action, expiresAt = null) {
    const permission = new Permission({
      userId,
      resourceType,
      resourceId,
      action,
      granted: true,
      expiresAt
    });
    
    return await permission.save();
  }

  async revokePermission(userId, resourceType, resourceId, action) {
    return await Permission.findOneAndDelete({
      userId,
      resourceType,
      resourceId,
      action
    });
  }

  async checkPermission(userId, resourceType, resourceId, action) {
    return await Permission.checkPermission(userId, resourceType, resourceId, action);
  }

  async getUserPermissions(userId, resourceType = null, resourceId = null) {
    const filter = { userId };
    
    if (resourceType) {
      filter.resourceType = resourceType;
    }
    
    if (resourceId) {
      filter.resourceId = resourceId;
    }
    
    return await Permission.find(filter)
      .populate('userId', 'username email avatar')
      .sort({ createdAt: -1 });
  }

  async getResourcePermissions(resourceType, resourceId) {
    return await Permission.find({
      resourceType,
      resourceId
    })
      .populate('userId', 'username email avatar')
      .sort({ createdAt: -1 });
  }

  async batchGrantPermissions(userId, permissions) {
    const docs = permissions.map(p => ({
      userId,
      resourceType: p.resourceType,
      resourceId: p.resourceId,
      action: p.action,
      granted: true,
      expiresAt: p.expiresAt || null
    }));
    
    return await Permission.insertMany(docs);
  }

  async batchRevokePermissions(userId, resourceType, resourceIds) {
    return await Permission.deleteMany({
      userId,
      resourceType,
      resourceId: { $in: resourceIds }
    });
  }

  async getDocumentPermissions(documentId) {
    const document = await Document.findById(documentId)
      .populate('owner', 'username email avatar role')
      .populate('collaborators.user', 'username email avatar role');
    
    if (!document) {
      return null;
    }
    
    const permissions = {
      owner: {
        user: document.owner,
        permissions: ['read', 'write', 'delete', 'manage', 'comment', 'resolve']
      },
      collaborators: document.collaborators.map(c => ({
        user: c.user,
        permission: c.permission,
        permissions: this.getPermissionsByRole(c.permission),
        addedAt: c.addedAt
      }))
    };
    
    return permissions;
  }

  getPermissionsByRole(role) {
    const permissionMap = {
      view: ['read'],
      comment: ['read', 'comment'],
      edit: ['read', 'comment', 'write', 'resolve'],
      owner: ['read', 'write', 'delete', 'manage', 'comment', 'resolve']
    };
    
    return permissionMap[role] || [];
  }

  async canManageDocument(documentId, userId) {
    const document = await Document.findById(documentId);
    
    if (!document) {
      return false;
    }
    
    return document.owner.toString() === userId;
  }

  async checkAnnotationAccess(annotation, userId, action) {
    const permission = await Permission.findOne({
      userId,
      resourceType: 'annotation',
      resourceId: annotation._id,
      action,
      granted: true
    });
    
    return !!permission;
  }

  async syncDocumentPermissions(documentId) {
    const document = await Document.findById(documentId);
    
    if (!document) {
      return false;
    }
    
    await Permission.deleteMany({
      resourceType: 'document',
      resourceId: documentId
    });
    
    const ownerPermissions = ['read', 'write', 'delete', 'manage', 'comment', 'resolve'];
    const ownerPermDocs = ownerPermissions.map(action => ({
      userId: document.owner,
      resourceType: 'document',
      resourceId: documentId,
      action,
      granted: true
    }));
    
    const collaboratorPermDocs = [];
    for (const collaborator of document.collaborators) {
      const permissions = this.getPermissionsByRole(collaborator.permission);
      for (const action of permissions) {
        collaboratorPermDocs.push({
          userId: collaborator.user,
          resourceType: 'document',
          resourceId: documentId,
          action,
          granted: true
        });
      }
    }
    
    await Permission.insertMany([...ownerPermDocs, ...collaboratorPermDocs]);
    
    return true;
  }
}

module.exports = new PermissionService();
