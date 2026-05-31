const Router = require('koa-router');
const ChunkController = require('../controllers/chunkController');

const router = new Router({ prefix: '/api/chunks' });

router.get('/verify', ChunkController.verifyChunk);
router.post('/upload', ChunkController.uploadChunk);
router.post('/merge', ChunkController.mergeChunks);
router.get('/:fileId', ChunkController.getUploadedChunks);

module.exports = router;
