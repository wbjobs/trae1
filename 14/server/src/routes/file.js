const Router = require('koa-router');
const FileController = require('../controllers/fileController');

const router = new Router({ prefix: '/api/files' });

router.post('/upload', FileController.upload);
router.get('/stats/overview', FileController.stats);
router.get('/', FileController.list);
router.get('/:id', FileController.detail);
router.get('/:id/download', FileController.download);
router.get('/:id/preview', FileController.preview);
router.delete('/:id', FileController.delete);

module.exports = router;
