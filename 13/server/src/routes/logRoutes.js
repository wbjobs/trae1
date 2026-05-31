const express = require('express');
const multer = require('multer');
const path = require('path');
const ctrl = require('../controllers/logController');
const config = require('../../config');

const router = express.Router();

const storage = multer.diskStorage({
  destination: (req, file, cb) => {
    const dir = path.join(__dirname, '../../uploads');
    require('fs').mkdirSync(dir, { recursive: true });
    cb(null, dir);
  },
  filename: (req, file, cb) => {
    const unique = Date.now() + '-' + Math.round(Math.random() * 1e9);
    cb(null, unique + path.extname(file.originalname));
  }
});

const upload = multer({
  storage,
  limits: { fileSize: config.upload.maxFileSize },
  fileFilter: (req, file, cb) => {
    const allowed = ['.log', '.txt', '.json', '.csv', '.xml', ''];
    const ext = path.extname(file.originalname).toLowerCase();
    if (allowed.includes(ext)) cb(null, true);
    else cb(new Error('不支持的文件类型'));
  }
});

router.get('/statistics', ctrl.getStatistics);
router.get('/compare', ctrl.compareLog);
router.get('/preview', ctrl.previewLog);
router.get('/audit', ctrl.listAuditLogs);
router.get('/audit/:id', ctrl.getAuditDetail);
router.get('/audit/:id/download', ctrl.downloadMasked);
router.get('/task/:taskId', ctrl.getTaskStatus);
router.post('/upload', upload.single('file'), ctrl.uploadLog);
router.post('/mask', ctrl.maskLog);
router.post('/mask/async', ctrl.startMaskTask);
router.post('/test', ctrl.testMask);

module.exports = router;
