const Router = require('koa-router');
const documentController = require('../controllers/documentController');
const { authMiddleware, optionalAuth } = require('../middleware/auth');

const router = new Router({ prefix: '/api/documents' });

router.post('/', authMiddleware, documentController.createDocument);
router.get('/', authMiddleware, documentController.listDocuments);
router.get('/:id', optionalAuth, documentController.getDocument);
router.put('/:id', authMiddleware, documentController.updateDocument);
router.delete('/:id', authMiddleware, documentController.deleteDocument);

router.post('/:id/collaborators', authMiddleware, documentController.addCollaborator);
router.delete('/:id/collaborators/:collaboratorId', authMiddleware, documentController.removeCollaborator);

router.get('/:id/versions', authMiddleware, documentController.getVersions);
router.get('/versions/:versionId', authMiddleware, documentController.getVersion);
router.post('/versions/:versionId/restore', authMiddleware, documentController.restoreVersion);

module.exports = router;
