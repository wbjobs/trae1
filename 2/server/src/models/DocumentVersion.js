const mongoose = require('mongoose');
const { Schema } = mongoose;

const DocumentVersionSchema = new Schema({
  documentId: {
    type: Schema.Types.ObjectId,
    ref: 'Document',
    required: true
  },
  version: {
    type: Number,
    required: true
  },
  title: {
    type: String,
    required: true
  },
  content: {
    type: String,
    required: true
  },
  createdBy: {
    type: Schema.Types.ObjectId,
    ref: 'User',
    required: true
  },
  changeDescription: {
    type: String,
    trim: true,
    maxlength: [500, '变更描述最多500个字符']
  },
  annotations: [{
    type: Schema.Types.ObjectId,
    ref: 'Annotation'
  }],
  metadata: {
    wordCount: Number,
    charCount: Number,
    size: Number
  }
}, {
  timestamps: true,
  versionKey: false
});

DocumentVersionSchema.index({ documentId: 1, version: -1 });

DocumentVersionSchema.methods.restore = async function() {
  const Document = mongoose.model('Document');
  const document = await Document.findById(this.documentId);
  
  if (document) {
    document.title = this.title;
    document.content = this.content;
    document.version = this.version + 1;
    await document.save();
    return document;
  }
  return null;
};

module.exports = mongoose.model('DocumentVersion', DocumentVersionSchema);
