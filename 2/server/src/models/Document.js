const mongoose = require('mongoose');
const { Schema } = mongoose;

const DocumentSchema = new Schema({
  title: {
    type: String,
    required: [true, '文档标题不能为空'],
    trim: true,
    maxlength: [200, '文档标题最多200个字符']
  },
  content: {
    type: String,
    required: [true, '文档内容不能为空']
  },
  type: {
    type: String,
    enum: ['doc', 'pdf', 'txt', 'markdown'],
    default: 'markdown'
  },
  owner: {
    type: Schema.Types.ObjectId,
    ref: 'User',
    required: true
  },
  collaborators: [{
    user: {
      type: Schema.Types.ObjectId,
      ref: 'User'
    },
    permission: {
      type: String,
      enum: ['view', 'comment', 'edit'],
      default: 'view'
    },
    addedAt: {
      type: Date,
      default: Date.now
    }
  }],
  status: {
    type: String,
    enum: ['draft', 'published', 'archived'],
    default: 'draft'
  },
  version: {
    type: Number,
    default: 1
  },
  tags: [{
    type: String,
    trim: true
  }],
  metadata: {
    size: Number,
    wordCount: Number,
    lastModifiedBy: {
      type: Schema.Types.ObjectId,
      ref: 'User'
    }
  }
}, {
  timestamps: true,
  versionKey: false
});

DocumentSchema.index({ title: 1, owner: 1, status: 1 });

DocumentSchema.methods.getPublicData = function() {
  const doc = this.toObject();
  delete doc.content;
  return doc;
};

module.exports = mongoose.model('Document', DocumentSchema);
