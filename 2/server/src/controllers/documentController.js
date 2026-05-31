const documentService = require('../services/documentService');
const { getSocketService } = require('../socket');

class DocumentController {
  async createDocument(ctx) {
    const { title, content, type, tags, metadata } = ctx.request.body;
    const userId = ctx.state.user.id;
    
    try {
      const document = await documentService.createDocument({
        title,
        content,
        type: type || 'markdown',
        tags: tags || [],
        metadata: metadata || {}
      }, userId);
      
      ctx.status = 201;
      ctx.body = {
        message: '文档创建成功',
        data: document
      };
    } catch (error) {
      console.error('[Controller] Create document error:', error);
      ctx.status = 500;
      ctx.body = { message: '创建文档失败，请稍后重试' };
    }
  }

  async getDocument(ctx) {
    const { id } = ctx.params;
    const userId = ctx.state.user?.id;
    
    try {
      const document = await documentService.getDocumentById(id, userId);
      
      if (!document) {
        ctx.status = 404;
        ctx.body = { message: '文档不存在或无访问权限' };
        return;
      }
      
      ctx.body = {
        data: document
      };
    } catch (error) {
      console.error('[Controller] Get document error:', error);
      ctx.status = 500;
      ctx.body = { message: '获取文档失败' };
    }
  }

  async listDocuments(ctx) {
    const { page = 1, limit = 20, status, type } = ctx.query;
    const userId = ctx.state.user?.id;
    
    try {
      const filter = {};
      if (status) filter.status = status;
      if (type) filter.type = type;
      
      const result = await documentService.getUserDocuments(userId, {
        page: parseInt(page),
        limit: parseInt(limit)
      });
      
      ctx.body = {
        data: result
      };
    } catch (error) {
      console.error('[Controller] List documents error:', error);
      ctx.status = 500;
      ctx.body = { message: '获取文档列表失败' };
    }
  }

  async updateDocument(ctx) {
    const { id } = ctx.params;
    const userId = ctx.state.user.id;
    const updateData = ctx.request.body;
    const expectedVersion = ctx.headers['x-expected-version'] 
      ? parseInt(ctx.headers['x-expected-version']) 
      : null;
    
    try {
      const result = await documentService.updateDocument(id, updateData, userId, expectedVersion);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { 
          message: result.error,
          ...(result.currentVersion !== undefined && { currentVersion: result.currentVersion })
        };
        return;
      }

      const socketService = getSocketService();
      if (socketService && id) {
        socketService.broadcastToDocument(id, 'document-edited', {
          documentId: id,
          content: updateData.content,
          version: result.data.version,
          editedBy: {
            id: userId,
            username: ctx.state.user.username
          },
          timestamp: Date.now()
        });
      }
      
      ctx.body = {
        message: '文档更新成功',
        data: result.data
      };
    } catch (error) {
      console.error('[Controller] Update document error:', error);
      ctx.status = 500;
      ctx.body = { message: '更新文档失败，请稍后重试' };
    }
  }

  async deleteDocument(ctx) {
    const { id } = ctx.params;
    const userId = ctx.state.user.id;
    
    try {
      const result = await documentService.deleteDocument(id, userId);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { message: result.error };
        return;
      }

      const socketService = getSocketService();
      if (socketService) {
        socketService.broadcastToDocument(id, 'document-deleted', {
          documentId: id,
          deletedBy: {
            id: userId,
            username: ctx.state.user.username
          },
          timestamp: Date.now()
        });
      }
      
      ctx.body = { message: '文档删除成功' };
    } catch (error) {
      console.error('[Controller] Delete document error:', error);
      ctx.status = 500;
      ctx.body = { message: '删除文档失败，请稍后重试' };
    }
  }

  async addCollaborator(ctx) {
    const { id } = ctx.params;
    const { userId: collaboratorId, permission } = ctx.request.body;
    const userId = ctx.state.user.id;
    
    try {
      const result = await documentService.addCollaborator(id, userId, collaboratorId, permission);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { message: result.error };
        return;
      }

      const socketService = getSocketService();
      if (socketService) {
        socketService.sendToUser(collaboratorId, 'collaborator-added', {
          documentId: id,
          addedBy: {
            id: userId,
            username: ctx.state.user.username
          },
          permission,
          timestamp: Date.now()
        });
      }
      
      ctx.body = {
        message: '协作者添加成功',
        data: result.data
      };
    } catch (error) {
      console.error('[Controller] Add collaborator error:', error);
      ctx.status = 500;
      ctx.body = { message: '添加协作者失败，请稍后重试' };
    }
  }

  async removeCollaborator(ctx) {
    const { id, collaboratorId } = ctx.params;
    const userId = ctx.state.user.id;
    
    try {
      const result = await documentService.removeCollaborator(id, userId, collaboratorId);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { message: result.error };
        return;
      }

      const socketService = getSocketService();
      if (socketService) {
        socketService.sendToUser(collaboratorId, 'collaborator-removed', {
          documentId: id,
          removedBy: {
            id: userId,
            username: ctx.state.user.username
          },
          timestamp: Date.now()
        });
      }
      
      ctx.body = {
        message: '协作者移除成功',
        data: result.data
      };
    } catch (error) {
      console.error('[Controller] Remove collaborator error:', error);
      ctx.status = 500;
      ctx.body = { message: '移除协作者失败，请稍后重试' };
    }
  }

  async getVersions(ctx) {
    const { id } = ctx.params;
    const { page = 1, limit = 10 } = ctx.query;
    const userId = ctx.state.user?.id;
    
    try {
      const result = await documentService.getDocumentVersions(id, userId, {
        page: parseInt(page),
        limit: parseInt(limit)
      });
      
      if (!result) {
        ctx.status = 404;
        ctx.body = { message: '文档不存在或无访问权限' };
        return;
      }
      
      ctx.body = {
        data: result
      };
    } catch (error) {
      console.error('[Controller] Get versions error:', error);
      ctx.status = 500;
      ctx.body = { message: '获取版本列表失败' };
    }
  }

  async getVersion(ctx) {
    const { versionId } = ctx.params;
    const userId = ctx.state.user?.id;
    
    try {
      const version = await documentService.getVersionById(versionId, userId);
      
      if (!version) {
        ctx.status = 404;
        ctx.body = { message: '版本不存在或无访问权限' };
        return;
      }
      
      ctx.body = {
        data: version
      };
    } catch (error) {
      console.error('[Controller] Get version error:', error);
      ctx.status = 500;
      ctx.body = { message: '获取版本详情失败' };
    }
  }

  async restoreVersion(ctx) {
    const { versionId } = ctx.params;
    const userId = ctx.state.user.id;
    
    try {
      const result = await documentService.restoreVersion(versionId, userId);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { message: result.error };
        return;
      }

      const socketService = getSocketService();
      if (socketService && result.data) {
        socketService.broadcastToDocument(result.data._id.toString(), 'document-restored', {
          documentId: result.data._id,
          version: result.data.version,
          restoredBy: {
            id: userId,
            username: ctx.state.user.username
          },
          timestamp: Date.now()
        });
      }
      
      ctx.body = {
        message: '版本恢复成功',
        data: result.data
      };
    } catch (error) {
      console.error('[Controller] Restore version error:', error);
      ctx.status = 500;
      ctx.body = { message: '恢复版本失败，请稍后重试' };
    }
  }
}

module.exports = new DocumentController();
