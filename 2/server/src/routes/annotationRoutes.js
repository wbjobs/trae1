const Router = require('koa-router');
const annotationController = require('../controllers/annotationController');
const { authMiddleware, optionalAuth } = require('../middleware/auth');

const router = new Router({ prefix: '/api/annotations' });

router.post('/', authMiddleware, annotationController.createAnnotation);
router.get('/document/:documentId', optionalAuth, annotationController.getDocumentAnnotations);
router.get('/document/:documentId/stats', optionalAuth, annotationController.getAnnotationStats);
router.get('/:id', optionalAuth, annotationController.getAnnotation);
router.put('/:id', authMiddleware, annotationController.updateAnnotation);
router.delete('/:id', authMiddleware, annotationController.deleteAnnotation);

router.post('/:id/resolve', authMiddleware, annotationController.resolveAnnotation);
router.post('/:id/unresolve', authMiddleware, annotationController.unresolveAnnotation);
router.post('/:id/reply', authMiddleware, annotationController.replyAnnotation);
router.post('/:id/reaction', authMiddleware, annotationController.addReaction);

router.get('/document/:documentId/search', optionalAuth, annotationController.searchAnnotations);
router.get('/document/:documentId/export', optionalAuth, annotationController.exportAnnotations);

module.exports = router;
