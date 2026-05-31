const Annotation = require('../models/Annotation');
const Document = require('../models/Document');
const Permission = require('../models/Permission');
const mongoose = require('mongoose');

class AnnotationService {
  async createAnnotation(annotationData, userId) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const { documentId, ...rest } = annotationData;
      
      const document = await Document.findById(documentId).session(session);
      if (!document) {
        await session.abortTransaction();
        return { success: false, error: '文档不存在', code: 404 };
      }
      
      const hasAccess = await this.checkDocumentAccess(document, userId, 'comment');
      if (!hasAccess) {
        await session.abortTransaction();
        return { success: false, error: '没有批注权限', code: 403 };
      }
      
      const annotation = new Annotation({
        ...rest,
        documentId,
        author: userId,
        version: 1
      });
      
      await annotation.save({ session });
      
      await annotation.populate('author', 'username email avatar');
      
      await session.commitTransaction();
      
      return { success: true, data: annotation };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Annotation] Create error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async getAnnotationsByDocument(documentId, userId, options = {}) {
    const { page = 1, limit = 50, status = 'active' } = options;
    const skip = (page - 1) * limit;
    
    const document = await Document.findById(documentId);
    if (!document) {
      return null;
    }
    
    const hasAccess = await this.checkDocumentAccess(document, userId, 'read');
    if (!hasAccess) {
      return null;
    }
    
    const filter = {
      documentId,
      status,
      $or: [
        { visibility: 'public' },
        { author: userId },
        { visibility: 'selected', visibleTo: userId }
      ]
    };
    
    const [annotations, total] = await Promise.all([
      Annotation.find(filter)
        .sort({ createdAt: -1 })
        .skip(skip)
        .limit(limit)
        .populate('author', 'username email avatar')
        .populate('replies')
        .populate('mentions', 'username email avatar'),
      Annotation.countDocuments(filter)
    ]);
    
    return {
      annotations,
      total,
      page,
      limit,
      totalPages: Math.ceil(total / limit)
    };
  }

  async getAnnotationById(id, userId) {
    const annotation = await Annotation.findById(id)
      .populate('author', 'username email avatar')
      .populate('replies')
      .populate('mentions', 'username email avatar');
    
    if (!annotation) {
      return null;
    }
    
    const canView = await this.canViewAnnotation(annotation, userId);
    if (!canView) {
      return null;
    }
    
    return annotation;
  }

  async updateAnnotation(id, updateData, userId, expectedVersion = null) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const annotation = await Annotation.findById(id).session(session);
      
      if (!annotation) {
        await session.abortTransaction();
        return { success: false, error: '批注不存在', code: 404 };
      }
      
      if (annotation.status === 'deleted') {
        await session.abortTransaction();
        return { success: false, error: '批注已被删除', code: 410 };
      }
      
      if (expectedVersion !== null && annotation.version !== expectedVersion) {
        await session.abortTransaction();
        return { 
          success: false, 
          error: '批注已被修改，请刷新后重试', 
          code: 409,
          currentVersion: annotation.version
        };
      }
      
      const canEdit = await this.canEditAnnotation(annotation, userId);
      if (!canEdit) {
        await session.abortTransaction();
        return { success: false, error: '没有编辑权限', code: 403 };
      }
      
      const updatePayload = {
        $set: { 
          ...updateData, 
          version: annotation.version + 1,
          updatedAt: new Date()
        }
      };
      
      const updated = await Annotation.findByIdAndUpdate(
        id,
        updatePayload,
        { new: true, runValidators: true, session }
      );
      
      await updated.populate('author', 'username email avatar');
      
      await session.commitTransaction();
      
      return { success: true, data: updated };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Annotation] Update error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async deleteAnnotation(id, userId, expectedVersion = null) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const annotation = await Annotation.findById(id).session(session);
      
      if (!annotation) {
        await session.abortTransaction();
        return { success: false, error: '批注不存在', code: 404 };
      }
      
      if (annotation.status === 'deleted') {
        await session.commitTransaction();
        return { success: true };
      }
      
      if (expectedVersion !== null && annotation.version !== expectedVersion) {
        await session.abortTransaction();
        return { 
          success: false, 
          error: '批注已被修改，请刷新后重试', 
          code: 409,
          currentVersion: annotation.version
        };
      }
      
      const canDelete = await this.canDeleteAnnotation(annotation, userId);
      if (!canDelete) {
        await session.abortTransaction();
        return { success: false, error: '没有删除权限', code: 403 };
      }
      
      await Annotation.findByIdAndUpdate(id, {
        $set: { status: 'deleted', updatedAt: new Date() }
      }, { session });
      
      await session.commitTransaction();
      
      return { success: true };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Annotation] Delete error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async resolveAnnotation(id, userId, expectedVersion = null) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const annotation = await Annotation.findById(id).session(session);
      
      if (!annotation) {
        await session.abortTransaction();
        return { success: false, error: '批注不存在', code: 404 };
      }
      
      if (annotation.status === 'deleted') {
        await session.abortTransaction();
        return { success: false, error: '批注已被删除', code: 410 };
      }
      
      if (expectedVersion !== null && annotation.version !== expectedVersion) {
        await session.abortTransaction();
        return { 
          success: false, 
          error: '批注已被修改，请刷新后重试', 
          code: 409,
          currentVersion: annotation.version
        };
      }
      
      const document = await Document.findById(annotation.documentId).session(session);
      const canResolve = await this.checkDocumentAccess(document, userId, 'resolve');
      
      if (!canResolve) {
        await session.abortTransaction();
        return { success: false, error: '没有解决权限', code: 403 };
      }
      
      const updated = await Annotation.findByIdAndUpdate(
        id,
        {
          $set: {
            status: 'resolved',
            resolvedAt: new Date(),
            resolvedBy: userId,
            version: annotation.version + 1
          }
        },
        { new: true, session }
      );
      
      await session.commitTransaction();
      
      return { success: true, data: updated };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Annotation] Resolve error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async unresolveAnnotation(id, userId) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const annotation = await Annotation.findById(id).session(session);
      
      if (!annotation) {
        await session.abortTransaction();
        return { success: false, error: '批注不存在', code: 404 };
      }
      
      if (annotation.status === 'deleted') {
        await session.abortTransaction();
        return { success: false, error: '批注已被删除', code: 410 };
      }
      
      const canEdit = await this.canEditAnnotation(annotation, userId);
      if (!canEdit) {
        await session.abortTransaction();
        return { success: false, error: '没有权限', code: 403 };
      }
      
      const updated = await Annotation.findByIdAndUpdate(
        id,
        {
          $set: { 
            status: 'active',
            version: annotation.version + 1
          },
          $unset: { resolvedAt: 1, resolvedBy: 1 }
        },
        { new: true, session }
      );
      
      await session.commitTransaction();
      
      return { success: true, data: updated };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Annotation] Unresolve error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async replyAnnotation(parentId, replyData, userId) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const parent = await Annotation.findById(parentId).session(session);
      
      if (!parent) {
        await session.abortTransaction();
        return { success: false, error: '父批注不存在', code: 404 };
      }
      
      if (parent.status === 'deleted') {
        await session.abortTransaction();
        return { success: false, error: '父批注已被删除', code: 410 };
      }
      
      const canView = await this.canViewAnnotation(parent, userId);
      if (!canView) {
        await session.abortTransaction();
        return { success: false, error: '没有查看权限', code: 403 };
      }
      
      const reply = new Annotation({
        ...replyData,
        documentId: parent.documentId,
        author: userId,
        parentId,
        visibility: parent.visibility,
        visibleTo: parent.visibleTo,
        version: 1
      });
      
      await reply.save({ session });
      
      parent.replies.push(reply._id);
      await parent.save({ session });
      
      await reply.populate('author', 'username email avatar');
      
      await session.commitTransaction();
      
      return { success: true, data: reply };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Annotation] Reply error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async addReaction(annotationId, userId, emoji) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const annotation = await Annotation.findById(annotationId).session(session);
      
      if (!annotation) {
        await session.abortTransaction();
        return { success: false, error: '批注不存在', code: 404 };
      }
      
      if (annotation.status === 'deleted') {
        await session.abortTransaction();
        return { success: false, error: '批注已被删除', code: 410 };
      }
      
      const existingReaction = annotation.reactions.find(
        r => r.user.toString() === userId && r.emoji === emoji
      );
      
      if (existingReaction) {
        annotation.reactions = annotation.reactions.filter(
          r => !(r.user.toString() === userId && r.emoji === emoji)
        );
      } else {
        annotation.reactions.push({ user: userId, emoji });
      }
      
      await annotation.save({ session });
      
      await session.commitTransaction();
      
      return { success: true, data: annotation };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Annotation] Add reaction error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async getAnnotationStats(documentId, userId) {
    const document = await Document.findById(documentId);
    if (!document) {
      return null;
    }
    
    const hasAccess = await this.checkDocumentAccess(document, userId, 'read');
    if (!hasAccess) {
      return null;
    }
    
    const stats = await Annotation.aggregate([
      { $match: { documentId: mongoose.Types.ObjectId(documentId), status: { $ne: 'deleted' } } },
      {
        $group: {
          _id: null,
          total: { $sum: 1 },
          active: { $sum: { $cond: [{ $eq: ['$status', 'active'] }, 1, 0] } },
          resolved: { $sum: { $cond: [{ $eq: ['$status', 'resolved'] }, 1, 0] } }
        }
      }
    ]);
    
    return stats[0] || { total: 0, active: 0, resolved: 0 };
  }

  async searchAnnotations(documentId, userId, searchOptions = {}) {
    const { keyword, status, type, startDate, endDate, author } = searchOptions;
    
    const document = await Document.findById(documentId);
    if (!document) {
      return { success: false, error: '文档不存在', code: 404 };
    }
    
    const hasAccess = await this.checkDocumentAccess(document, userId, 'read');
    if (!hasAccess) {
      return { success: false, error: '没有访问权限', code: 403 };
    }
    
    const filter = {
      documentId: mongoose.Types.ObjectId(documentId),
      status: { $ne: 'deleted' },
      $or: [
        { visibility: 'public' },
        { author: mongoose.Types.ObjectId(userId) },
        { visibility: 'selected', visibleTo: mongoose.Types.ObjectId(userId) }
      ]
    };
    
    if (keyword) {
      filter.$text = { $search: keyword };
    }
    
    if (status && status !== 'all') {
      filter.status = status;
    }
    
    if (type && type !== 'all') {
      filter.type = type;
    }
    
    if (author) {
      filter.author = mongoose.Types.ObjectId(author);
    }
    
    if (startDate || endDate) {
      filter.createdAt = {};
      if (startDate) {
        filter.createdAt.$gte = new Date(startDate);
      }
      if (endDate) {
        filter.createdAt.$lte = new Date(endDate);
      }
    }
    
    const query = Annotation.find(filter).populate('author', 'username email avatar');
    
    if (keyword) {
      query.select({ score: { $meta: 'textScore' } });
      query.sort({ score: { $meta: 'textScore' } });
    } else {
      query.sort({ createdAt: -1 });
    }
    
    const annotations = await query.lean();
    
    return { success: true, data: annotations };
  }

  async exportAnnotations(documentId, userId, exportOptions = {}) {
    const { format = 'json', includeResolved = true } = exportOptions;
    
    const document = await Document.findById(documentId);
    if (!document) {
      return { success: false, error: '文档不存在', code: 404 };
    }
    
    const hasAccess = await this.checkDocumentAccess(document, userId, 'read');
    if (!hasAccess) {
      return { success: false, error: '没有访问权限', code: 403 };
    }
    
    const filter = {
      documentId: mongoose.Types.ObjectId(documentId),
      $or: [
        { visibility: 'public' },
        { author: mongoose.Types.ObjectId(userId) },
        { visibility: 'selected', visibleTo: mongoose.Types.ObjectId(userId) }
      ]
    };
    
    if (!includeResolved) {
      filter.status = 'active';
    } else {
      filter.status = { $ne: 'deleted' };
    }
    
    const annotations = await Annotation.find(filter)
      .populate('author', 'username email avatar')
      .populate('replies')
      .sort({ createdAt: 1 })
      .lean();
    
    let content;
    let contentType;
    let filename;
    
    switch (format) {
      case 'csv':
        content = this.generateCSV(annotations, document);
        contentType = 'text/csv; charset=utf-8';
        filename = `annotations_${documentId}_${Date.now()}.csv`;
        break;
        
      case 'markdown':
        content = this.generateMarkdown(annotations, document);
        contentType = 'text/markdown; charset=utf-8';
        filename = `annotations_${documentId}_${Date.now()}.md`;
        break;
        
      default:
        content = JSON.stringify({
          document: {
            id: document._id,
            title: document.title
          },
          exportedAt: new Date().toISOString(),
          totalAnnotations: annotations.length,
          annotations: annotations.map(a => ({
            id: a._id,
            content: a.content,
            type: a.type,
            status: a.status,
            author: a.author ? {
              id: a.author._id,
              username: a.author.username
            } : null,
            createdAt: a.createdAt,
            resolvedAt: a.resolvedAt,
            suggestion: a.suggestion,
            replies: (a.replies || []).map(r => ({
              id: r._id,
              content: r.content,
              createdAt: r.createdAt
            }))
          }))
        }, null, 2);
        contentType = 'application/json; charset=utf-8';
        filename = `annotations_${documentId}_${Date.now()}.json`;
    }
    
    return {
      success: true,
      data: {
        content,
        contentType,
        filename,
        totalCount: annotations.length
      }
    };
  }

  generateCSV(annotations, document) {
    const headers = ['ID', '内容', '类型', '状态', '作者', '创建时间', '解决时间', '建议内容', '回复数'];
    const rows = annotations.map(a => [
      a._id,
      `"${(a.content || '').replace(/"/g, '""')}"`,
      a.type || '',
      a.status || '',
      a.author?.username || '',
      new Date(a.createdAt).toLocaleString('zh-CN'),
      a.resolvedAt ? new Date(a.resolvedAt).toLocaleString('zh-CN') : '',
      `"${(a.suggestion || '').replace(/"/g, '""')}"`,
      (a.replies || []).length
    ]);
    
    return '\uFEFF' + [headers.join(','), ...rows.map(r => r.join(','))].join('\n');
  }

  generateMarkdown(annotations, document) {
    let md = `# 文档批注导出\n\n`;
    md += `**文档标题**: ${document.title}\n`;
    md += `**导出时间**: ${new Date().toLocaleString('zh-CN')}\n`;
    md += `**批注总数**: ${annotations.length}\n\n`;
    md += `---\n\n`;
    
    annotations.forEach((a, index) => {
      const statusIcon = a.status === 'resolved' ? '✅' : '📌';
      const typeLabel = {
        comment: '💬 评论',
        suggestion: '💡 建议',
        issue: '🐛 问题',
        task: '✅ 任务'
      }[a.type] || a.type;
      
      md += `## ${statusIcon} 批注 #${index + 1}\n\n`;
      md += `**类型**: ${typeLabel}\n`;
      md += `**状态**: ${a.status === 'resolved' ? '已解决' : '活跃'}\n`;
      md += `**作者**: ${a.author?.username || '未知'}\n`;
      md += `**创建时间**: ${new Date(a.createdAt).toLocaleString('zh-CN')}\n`;
      
      if (a.resolvedAt) {
        md += `**解决时间**: ${new Date(a.resolvedAt).toLocaleString('zh-CN')}\n`;
      }
      
      md += `\n**内容**:\n\n${a.content}\n\n`;
      
      if (a.suggestion) {
        md += `**建议内容**:\n\n${a.suggestion}\n\n`;
      }
      
      if (a.replies && a.replies.length > 0) {
        md += `**回复** (${a.replies.length}条):\n\n`;
        a.replies.forEach((r, i) => {
          md += `${i + 1}. ${r.content}\n`;
        });
        md += '\n';
      }
      
      md += '---\n\n';
    });
    
    return md;
  }

  async checkDocumentAccess(document, userId, action) {
    if (!document || !userId) {
      return false;
    }
    
    if (document.owner.toString() === userId) {
      return true;
    }
    
    const collaborator = document.collaborators.find(
      c => c.user.toString() === userId
    );
    
    if (!collaborator) {
      return false;
    }
    
    const permissionMap = {
      view: ['read'],
      comment: ['read', 'comment'],
      edit: ['read', 'comment', 'write', 'resolve']
    };
    
    return permissionMap[collaborator.permission]?.includes(action) || false;
  }

  async canViewAnnotation(annotation, userId) {
    if (annotation.visibility === 'public') {
      return true;
    }
    
    if (annotation.author.toString() === userId) {
      return true;
    }
    
    if (annotation.visibility === 'selected') {
      return annotation.visibleTo.some(user => user.toString() === userId);
    }
    
    return true;
  }

  async canEditAnnotation(annotation, userId) {
    if (annotation.author.toString() === userId) {
      return true;
    }
    
    const document = await Document.findById(annotation.documentId);
    return document && document.owner.toString() === userId;
  }

  async canDeleteAnnotation(annotation, userId) {
    return await this.canEditAnnotation(annotation, userId);
  }
}

module.exports = new AnnotationService();
