const Router = require('koa-router');
const ShareController = require('../controllers/shareController');

const router = new Router({ prefix: '/api/shares' });

router.post('/', ShareController.create);
router.post('/:shareCode/access', ShareController.access);
router.get('/', ShareController.list);
router.get('/:id', ShareController.detail);
router.post('/:id/revoke', ShareController.revoke);

module.exports = router;
