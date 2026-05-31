const express = require('express');
const ctrl = require('../controllers/ruleController');

const router = express.Router();

router.get('/', ctrl.listRules);
router.get('/templates', ctrl.getTemplateSamples);
router.get('/export', ctrl.exportRules);
router.get('/:id', ctrl.getRule);
router.post('/', ctrl.createRule);
router.post('/import', ctrl.importRules);
router.post('/batch/toggle', ctrl.batchToggle);
router.post('/batch/delete', ctrl.batchDelete);
router.put('/:id', ctrl.updateRule);
router.put('/:id/toggle', ctrl.toggleRule);
router.delete('/:id', ctrl.deleteRule);

module.exports = router;
