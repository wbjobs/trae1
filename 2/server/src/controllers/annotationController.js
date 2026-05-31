const annotationService = require('../services/annotationService');
const { getSocketService } = require('../socket');

class AnnotationController {
  async createAnnotation(ctx) {
    const userId = ctx.state.user.id;
    const annotationData = ctx.request.body;
    
    try {
      const result = await annotationService.createAnnotation(annotationData, userId);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { message: result.error };
        return;
      }

      const socketService = getSocketService();
      if (socketService && annotationData.documentId) {
        socketService.broadcastToDocument(annotationData.documentId, 'annotation-created', {
          annotation: result.data,
          createdBy: {
            id: userId,
            username: ctx.state.user.username
          },
          timestamp: Date.now()
        });
      }
      
      ctx.status = 201;
      ctx.body = {
        message: '批注创建成功',
        data: result.data
      };
    } catch (error) {
      console.error('[Controller] Create annotation error:', error);
      ctx.status = 500;
      ctx.body = { message: '创建批注失败，请稍后重试' };
    }
  }

  async getDocumentAnnotations(ctx) {
    const { documentId } = ctx.params;
    const { page = 1, limit = 50, status } = ctx.query;
    const userId = ctx.state.user?.id;
    
    try {
      const result = await annotationService.getAnnotationsByDocument(
        documentId,
        userId,
        {
          page: parseInt(page),
          limit: parseInt(limit),
          status: status || 'active'
        }
      );
      
      if (!result) {
        ctx.status = 404;
        ctx.body = { message: '文档不存在或无访问权限' };
        return;
      }
      
      ctx.body = {
        data: result
      };
    } catch (error) {
      console.error('[Controller] Get annotations error:', error);
      ctx.status = 500;
      ctx.body = { message: '获取批注列表失败' };
    }
  }

  async getAnnotation(ctx) {
    const { id } = ctx.params;
    const userId = ctx.state.user?.id;
    
    try {
      const annotation = await annotationService.getAnnotationById(id, userId);
      
      if (!annotation) {
        ctx.status = 404;
        ctx.body = { message: '批注不存在或无访问权限' };
        return;
      }
      
      ctx.body = {
        data: annotation
      };
    } catch (error) {
      console.error('[Controller] Get annotation error:', error);
      ctx.status = 500;
      ctx.body = { message: '获取批注详情失败' };
    }
  }

  async updateAnnotation(ctx) {
    const { id } = ctx.params;
    const userId = ctx.state.user.id;
    const updateData = ctx.request.body;
    const expectedVersion = ctx.headers['x-expected-version'] 
      ? parseInt(ctx.headers['x-expected-version']) 
      : null;
    
    try {
      const result = await annotationService.updateAnnotation(id, updateData, userId, expectedVersion);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { 
          message: result.error,
          ...(result.currentVersion !== undefined && { currentVersion: result.currentVersion })
        };
        return;
      }

      const socketService = getSocketService();
      if (socketService && result.data.documentId) {
        socketService.broadcastToDocument(result.data.documentId.toString(), 'annotation-updated', {
          annotationId: id,
          updates: updateData,
          version: result.data.version,
          updatedBy: {
            id: userId,
            username: ctx.state.user.username
          },
          timestamp: Date.now()
        });
      }
      
      ctx.body = {
        message: '批注更新成功',
        data: result.data
      };
    } catch (error) {
      console.error('[Controller] Update annotation error:', error);
      ctx.status = 500;
      ctx.body = { message: '更新批注失败，请稍后重试' };
    }
  }

  async deleteAnnotation(ctx) {
    const { id } = ctx.params;
    const userId = ctx.state.user.id;
    const expectedVersion = ctx.headers['x-expected-version'] 
      ? parseInt(ctx.headers['x-expected-version']) 
      : null;
    
    try {
      const result = await annotationService.deleteAnnotation(id, userId, expectedVersion);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { 
          message: result.error,
          ...(result.currentVersion !== undefined && { currentVersion: result.currentVersion })
        };
        return;
      }

      const socketService = getSocketService();
      if (socketService) {
        const annotation = await annotationService.getAnnotationById(id, userId);
        if (annotation?.documentId) {
          socketService.broadcastToDocument(annotation.documentId.toString(), 'annotation-deleted', {
            annotationId: id,
            deletedBy: {
              id: userId,
              username: ctx.state.user.username
            },
            timestamp: Date.now()
          });
        }
      }
      
      ctx.body = { message: '批注删除成功' };
    } catch (error) {
      console.error('[Controller] Delete annotation error:', error);
      ctx.status = 500;
      ctx.body = { message: '删除批注失败，请稍后重试' };
    }
  }

  async resolveAnnotation(ctx) {
    const { id } = ctx.params;
    const userId = ctx.state.user.id;
    const expectedVersion = ctx.headers['x-expected-version'] 
      ? parseInt(ctx.headers['x-expected-version']) 
      : null;
    
    try {
      const result = await annotationService.resolveAnnotation(id, userId, expectedVersion);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { 
          message: result.error,
          ...(result.currentVersion !== undefined && { currentVersion: result.currentVersion })
        };
        return;
      }

      const socketService = getSocketService();
      if (socketService && result.data?.documentId) {
        socketService.broadcastToDocument(result.data.documentId.toString(), 'annotation-resolved', {
          annotationId: id,
          resolvedBy: {
            id: userId,
            username: ctx.state.user.username
          },
          timestamp: Date.now()
        });
      }
      
      ctx.body = {
        message: '批注已解决',
        data: result.data
      };
    } catch (error) {
      console.error('[Controller] Resolve annotation error:', error);
      ctx.status = 500;
      ctx.body = { message: '解决批注失败，请稍后重试' };
    }
  }

  async unresolveAnnotation(ctx) {
    const { id } = ctx.params;
    const userId = ctx.state.user.id;
    
    try {
      const result = await annotationService.unresolveAnnotation(id, userId);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { message: result.error };
        return;
      }
      
      ctx.body = {
        message: '批注已取消解决',
        data: result.data
      };
    } catch (error) {
      console.error('[Controller] Unresolve annotation error:', error);
      ctx.status = 500;
      ctx.body = { message: '取消解决失败，请稍后重试' };
    }
  }

  async replyAnnotation(ctx) {
    const { id } = ctx.params;
    const userId = ctx.state.user.id;
    const { content, type } = ctx.request.body;
    
    try {
      const result = await annotationService.replyAnnotation(id, { content, type }, userId);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { message: result.error };
        return;
      }

      const socketService = getSocketService();
      if (socketService && result.data?.documentId) {
        socketService.broadcastToDocument(result.data.documentId.toString(), 'annotation-replied', {
          parentId: id,
          reply: result.data,
          repliedBy: {
            id: userId,
            username: ctx.state.user.username
          },
          timestamp: Date.now()
        });
      }
      
      ctx.status = 201;
      ctx.body = {
        message: '回复成功',
        data: result.data
      };
    } catch (error) {
      console.error('[Controller] Reply annotation error:', error);
      ctx.status = 500;
      ctx.body = { message: '回复失败，请稍后重试' };
    }
  }

  async addReaction(ctx) {
    const { id } = ctx.params;
    const userId = ctx.state.user.id;
    const { emoji } = ctx.request.body;
    
    try {
      const result = await annotationService.addReaction(id, userId, emoji);
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { message: result.error };
        return;
      }
      
      ctx.body = {
        message: '反应添加成功',
        data: result.data
      };
    } catch (error) {
      console.error('[Controller] Add reaction error:', error);
      ctx.status = 500;
      ctx.body = { message: '添加反应失败，请稍后重试' };
    }
  }

  async getAnnotationStats(ctx) {
    const { documentId } = ctx.params;
    const userId = ctx.state.user?.id;
    
    try {
      const stats = await annotationService.getAnnotationStats(documentId, userId);
      
      if (!stats) {
        ctx.status = 404;
        ctx.body = { message: '文档不存在或无访问权限' };
        return;
      }
      
      ctx.body = {
        data: stats
      };
    } catch (error) {
      console.error('[Controller] Get annotation stats error:', error);
      ctx.status = 500;
      ctx.body = { message: '获取批注统计失败' };
    }
  }

  async searchAnnotations(ctx) {
    const { documentId } = ctx.params;
    const userId = ctx.state.user?.id;
    const { keyword, status, type, startDate, endDate, author } = ctx.query;
    
    try {
      const result = await annotationService.searchAnnotations(documentId, userId, {
        keyword, status, type, startDate, endDate, author
      });
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { message: result.error };
        return;
      }
      
      ctx.body = {
        data: result.data,
        total: result.data.length
      };
    } catch (error) {
      console.error('[Controller] Search annotations error:', error);
      ctx.status = 500;
      ctx.body = { message: '搜索批注失败' };
    }
  }

  async exportAnnotations(ctx) {
    const { documentId } = ctx.params;
    const userId = ctx.state.user?.id;
    const { format = 'json', includeResolved = 'true' } = ctx.query;
    
    try {
      const result = await annotationService.exportAnnotations(documentId, userId, {
        format,
        includeResolved: includeResolved === 'true'
      });
      
      if (!result.success) {
        ctx.status = result.code || 400;
        ctx.body = { message: result.error };
        return;
      }
      
      ctx.set('Content-Type', result.data.contentType);
      ctx.set('Content-Disposition', `attachment; filename="${encodeURIComponent(result.data.filename)}"`);
      ctx.body = result.data.content;
    } catch (error) {
      console.error('[Controller] Export annotations error:', error);
      ctx.status = 500;
      ctx.body = { message: '导出批注失败' };
    }
  }
}

module.exports = new AnnotationController();
