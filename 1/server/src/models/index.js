const User = require('./User');
const Role = require('./Role');
const Permission = require('./Permission');
const UserRole = require('./UserRole');
const RolePermission = require('./RolePermission');
const OperationLog = require('./OperationLog');
const AuditLog = require('./AuditLog');
const SystemConfig = require('./SystemConfig');

User.belongsToMany(Role, {
  through: UserRole,
  foreignKey: 'userId',
  otherKey: 'roleId',
  as: 'roles'
});

Role.belongsToMany(User, {
  through: UserRole,
  foreignKey: 'roleId',
  otherKey: 'userId',
  as: 'users'
});

Role.belongsToMany(Permission, {
  through: RolePermission,
  foreignKey: 'roleId',
  otherKey: 'permissionId',
  as: 'permissions'
});

Permission.belongsToMany(Role, {
  through: RolePermission,
  foreignKey: 'permissionId',
  otherKey: 'roleId',
  as: 'roles'
});

Permission.hasMany(Permission, {
  foreignKey: 'parentId',
  as: 'children'
});

Permission.belongsTo(Permission, {
  foreignKey: 'parentId',
  as: 'parent'
});

module.exports = {
  User,
  Role,
  Permission,
  UserRole,
  RolePermission,
  OperationLog,
  AuditLog,
  SystemConfig
};
