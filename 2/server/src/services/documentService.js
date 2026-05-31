const Document = require('../models/Document');
const DocumentVersion = require('../models/DocumentVersion');
const Permission = require('../models/Permission');
const mongoose = require('mongoose');

class DocumentService {
  async createDocument(documentData, userId) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const document = new Document({
        ...documentData,
        owner: userId,
        version: 1,
        metadata: {
          ...documentData.metadata,
          lastModifiedBy: userId
        }
      });
      
      await document.save({ session });
      
      await this.createVersion(document, userId, '初始版本', session);
      
      await session.commitTransaction();
      
      return document;
    } catch (error) {
      await session.abortTransaction();
      console.error('[Document] Create error:', error);
      throw error;
    } finally {
      session.endSession();
    }
  }

  async getDocumentById(id, userId) {
    const document = await Document.findById(id)
      .populate('owner', 'username email avatar')
      .populate('collaborators.user', 'username email avatar');
    
    if (!document) {
      return null;
    }
    
    const hasAccess = await this.checkAccess(document, userId, 'read');
    if (!hasAccess) {
      return null;
    }
    
    return document;
  }

  async listDocuments(filter = {}, options = {}) {
    const { page = 1, limit = 20, sort = { updatedAt: -1 } } = options;
    const skip = (page - 1) * limit;
    
    const [documents, total] = await Promise.all([
      Document.find(filter)
        .sort(sort)
        .skip(skip)
        .limit(limit)
        .populate('owner', 'username email avatar'),
      Document.countDocuments(filter)
    ]);
    
    return {
      documents: documents.map(doc => doc.getPublicData()),
      total,
      page,
      limit,
      totalPages: Math.ceil(total / limit)
    };
  }

  async getUserDocuments(userId, options = {}) {
    const filter = {
      $or: [
        { owner: userId },
        { 'collaborators.user': userId }
      ]
    };
    
    return await this.listDocuments(filter, options);
  }

  async updateDocument(id, updateData, userId, expectedVersion = null) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const document = await Document.findById(id).session(session);
      
      if (!document) {
        await session.abortTransaction();
        return { success: false, error: '文档不存在', code: 404 };
      }
      
      if (expectedVersion !== null && document.version !== expectedVersion) {
        await session.abortTransaction();
        return { 
          success: false, 
          error: '文档已被修改，请刷新后重试', 
          code: 409,
          currentVersion: document.version
        };
      }
      
      const hasAccess = await this.checkAccess(document, userId, 'write');
      if (!hasAccess) {
        await session.abortTransaction();
        return { success: false, error: '没有编辑权限', code: 403 };
      }
      
      const oldContent = document.content;
      const oldTitle = document.title;
      
      const newVersion = document.version + 1;
      
      const updated = await Document.findByIdAndUpdate(
        id,
        { 
          $set: {
            ...updateData,
            version: newVersion,
            'metadata.lastModifiedBy': userId,
            updatedAt: new Date()
          }
        },
        { new: true, runValidators: true, session }
      );
      
      if (updated && (updateData.content !== oldContent || updateData.title !== oldTitle)) {
        await this.createVersion(updated, userId, updateData.changeDescription || '内容更新', session);
      }
      
      await session.commitTransaction();
      
      return { success: true, data: updated };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Document] Update error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async deleteDocument(id, userId) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const document = await Document.findById(id).session(session);
      
      if (!document) {
        await session.abortTransaction();
        return { success: false, error: '文档不存在', code: 404 };
      }
      
      const isOwner = document.owner.toString() === userId;
      if (!isOwner) {
        await session.abortTransaction();
        return { success: false, error: '没有删除权限', code: 403 };
      }
      
      await Promise.all([
        Document.findByIdAndDelete(id, { session }),
        DocumentVersion.deleteMany({ documentId: id }, { session })
      ]);
      
      await session.commitTransaction();
      
      return { success: true };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Document] Delete error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async addCollaborator(documentId, userId, collaboratorId, permission = 'view') {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const document = await Document.findById(documentId).session(session);
      
      if (!document) {
        await session.abortTransaction();
        return { success: false, error: '文档不存在', code: 404 };
      }
      
      const isOwner = document.owner.toString() === userId;
      if (!isOwner) {
        await session.abortTransaction();
        return { success: false, error: '没有管理权限', code: 403 };
      }
      
      if (collaboratorId === userId) {
        await session.abortTransaction();
        return { success: false, error: '不能添加自己为协作者', code: 400 };
      }
      
      const existingCollaborator = document.collaborators.find(
        c => c.user.toString() === collaboratorId
      );
      
      if (existingCollaborator) {
        existingCollaborator.permission = permission;
      } else {
        document.collaborators.push({
          user: collaboratorId,
          permission
        });
      }
      
      await document.save({ session });
      
      await Permission.findOneAndUpdate(
        {
          userId: collaboratorId,
          resourceType: 'document',
          resourceId: documentId
        },
        { action: permission === 'edit' ? 'write' : 'read', granted: true },
        { upsert: true, new: true, session }
      );
      
      await session.commitTransaction();
      
      return { success: true, data: document };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Document] Add collaborator error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async removeCollaborator(documentId, userId, collaboratorId) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const document = await Document.findById(documentId).session(session);
      
      if (!document) {
        await session.abortTransaction();
        return { success: false, error: '文档不存在', code: 404 };
      }
      
      const isOwner = document.owner.toString() === userId;
      if (!isOwner) {
        await session.abortTransaction();
        return { success: false, error: '没有管理权限', code: 403 };
      }
      
      document.collaborators = document.collaborators.filter(
        c => c.user.toString() !== collaboratorId
      );
      
      await document.save({ session });
      
      await Permission.deleteMany({
        userId: collaboratorId,
        resourceType: 'document',
        resourceId: documentId
      }, { session });
      
      await session.commitTransaction();
      
      return { success: true, data: document };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Document] Remove collaborator error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async createVersion(document, userId, description, session = null) {
    const version = new DocumentVersion({
      documentId: document._id,
      version: document.version,
      title: document.title,
      content: document.content,
      createdBy: userId,
      changeDescription: description,
      metadata: {
        wordCount: document.content?.length || 0,
        ...document.metadata
      }
    });
    
    if (session) {
      return await version.save({ session });
    }
    return await version.save();
  }

  async getDocumentVersions(documentId, userId, options = {}) {
    const { page = 1, limit = 10 } = options;
    const skip = (page - 1) * limit;
    
    const document = await Document.findById(documentId);
    if (!document) {
      return null;
    }
    
    const hasAccess = await this.checkAccess(document, userId, 'read');
    if (!hasAccess) {
      return null;
    }
    
    const [versions, total] = await Promise.all([
      DocumentVersion.find({ documentId })
        .sort({ version: -1 })
        .skip(skip)
        .limit(limit)
        .populate('createdBy', 'username email avatar'),
      DocumentVersion.countDocuments({ documentId })
    ]);
    
    return {
      versions,
      total,
      page,
      limit,
      totalPages: Math.ceil(total / limit)
    };
  }

  async getVersionById(versionId, userId) {
    const version = await DocumentVersion.findById(versionId)
      .populate('createdBy', 'username email avatar');
    
    if (!version) {
      return null;
    }
    
    const document = await Document.findById(version.documentId);
    if (!document) {
      return null;
    }
    
    const hasAccess = await this.checkAccess(document, userId, 'read');
    if (!hasAccess) {
      return null;
    }
    
    return version;
  }

  async restoreVersion(versionId, userId) {
    const session = await mongoose.startSession();
    
    try {
      session.startTransaction();
      
      const version = await DocumentVersion.findById(versionId).session(session);
      
      if (!version) {
        await session.abortTransaction();
        return { success: false, error: '版本不存在', code: 404 };
      }
      
      const document = await Document.findById(version.documentId).session(session);
      if (!document) {
        await session.abortTransaction();
        return { success: false, error: '文档不存在', code: 404 };
      }
      
      const hasAccess = await this.checkAccess(document, userId, 'write');
      if (!hasAccess) {
        await session.abortTransaction();
        return { success: false, error: '没有恢复权限', code: 403 };
      }
      
      const snapshotVersion = document.version + 1;
      
      await this.createVersion(document, userId, `恢复前快照（版本${document.version}）`, session);
      
      document.title = version.title;
      document.content = version.content;
      document.version = snapshotVersion + 1;
      document.metadata.lastModifiedBy = userId;
      
      await document.save({ session });
      
      await session.commitTransaction();
      
      return { success: true, data: document };
    } catch (error) {
      await session.abortTransaction();
      console.error('[Document] Restore version error:', error);
      return { success: false, error: error.message, code: 500 };
    } finally {
      session.endSession();
    }
  }

  async checkAccess(document, userId, action) {
    if (!userId) {
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

  async getAccessibleDocuments(userId, options = {}) {
    return await this.getUserDocuments(userId, options);
  }
}

module.exports = new DocumentService();
