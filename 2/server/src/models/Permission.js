const mongoose = require('mongoose');
const { Schema } = mongoose;

const PermissionSchema = new Schema({
  userId: {
    type: Schema.Types.ObjectId,
    ref: 'User',
    required: true
  },
  resourceType: {
    type: String,
    enum: ['document', 'annotation', 'system'],
    required: true
  },
  resourceId: {
    type: Schema.Types.ObjectId,
    required: true
  },
  action: {
    type: String,
    enum: ['read', 'write', 'delete', 'manage', 'comment', 'resolve'],
    required: true
  },
  granted: {
    type: Boolean,
    default: true
  },
  expiresAt: {
    type: Date
  },
  conditions: {
    type: Schema.Types.Mixed
  }
}, {
  timestamps: true,
  versionKey: false
});

PermissionSchema.index({ userId: 1, resourceType: 1, resourceId: 1 });

PermissionSchema.statics.checkPermission = async function(userId, resourceType, resourceId, action) {
  const permission = await this.findOne({
    userId,
    resourceType,
    resourceId,
    action,
    granted: true,
    $or: [
      { expiresAt: { $gt: new Date() } },
      { expiresAt: null }
    ]
  });
  return !!permission;
};

module.exports = mongoose.model('Permission', PermissionSchema);
