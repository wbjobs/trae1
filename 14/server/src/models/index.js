const File = require('./File');
const Share = require('./Share');
const Chunk = require('./Chunk');

File.hasMany(Share, { foreignKey: 'fileId', as: 'shares' });
Share.belongsTo(File, { foreignKey: 'fileId', as: 'file' });

module.exports = {
  File,
  Share,
  Chunk
};
