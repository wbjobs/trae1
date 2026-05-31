const mongoose = require('mongoose');
const { Schema } = mongoose;

const AnnotationSchema = new Schema({
  documentId: {
    type: Schema.Types.ObjectId,
    ref: 'Document',
    required: true
  },
  author: {
    type: Schema.Types.ObjectId,
    ref: 'User',
    required: true
  },
  type: {
    type: String,
    enum: ['highlight', 'comment', 'sticky-note', 'underline', 'suggestion'],
    default: 'comment'
  },
  content: {
    type: String,
    required: [true, '批注内容不能为空'],
    trim: true,
    maxlength: [2000, '批注内容最多2000个字符']
  },
  position: {
    start: {
      type: Number,
      required: true
    },
    end: {
      type: Number,
      required: true
    },
    selectionText: {
      type: String,
      trim: true
    },
    page: {
      type: Number,
      default: 1
    },
    coordinates: {
      x: Number,
      y: Number
    }
  },
  visibility: {
    type: String,
    enum: ['public', 'private', 'selected'],
    default: 'public'
  },
  visibleTo: [{
    type: Schema.Types.ObjectId,
    ref: 'User'
  }],
  status: {
    type: String,
    enum: ['active', 'resolved', 'deleted'],
    default: 'active'
  },
  resolvedAt: {
    type: Date
  },
  resolvedBy: {
    type: Schema.Types.ObjectId,
    ref: 'User'
  },
  parentId: {
    type: Schema.Types.ObjectId,
    ref: 'Annotation',
    default: null
  },
  replies: [{
    type: Schema.Types.ObjectId,
    ref: 'Annotation'
  }],
  mentions: [{
    type: Schema.Types.ObjectId,
    ref: 'User'
  }],
  reactions: [{
    user: {
      type: Schema.Types.ObjectId,
      ref: 'User'
    },
    emoji: {
      type: String
    },
    createdAt: {
      type: Date,
      default: Date.now
    }
  }],
  color: {
    type: String,
    default: '#FFEB3B'
  },
  version: {
    type: Number,
    default: 1
  }
}, {
  timestamps: true,
  versionKey: false
});

AnnotationSchema.index({ documentId: 1, author: 1, status: 1 });
AnnotationSchema.index({ parentId: 1 });
AnnotationSchema.index({ content: 'text', type: 1, status: 1 });

module.exports = mongoose.model('Annotation', AnnotationSchema);
