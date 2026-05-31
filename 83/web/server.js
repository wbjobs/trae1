const express = require('express');
const Database = require('better-sqlite3');
const cors = require('cors');
const path = require('path');
const fs = require('fs');

const app = express();
const PORT = process.env.PORT || 3000;
const DB_PATH = process.env.DB_PATH || path.join(__dirname, '..', 'data', 'audit.db');
const RECORDINGS_DIR = process.env.RECORDINGS_DIR || path.join(__dirname, '..', 'recordings');

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));
app.use('/recordings', express.static(RECORDINGS_DIR));

function getDb() {
    return new Database(DB_PATH, { readonly: true });
}

app.get('/api/sessions', (req, res) => {
    try {
        const db = getDb();
        const rows = db.prepare(`
            SELECT s.*,
                (SELECT COUNT(*) FROM keyboard_events k WHERE k.session_id = s.session_id) as key_count,
                (SELECT COUNT(*) FROM mouse_events m WHERE m.session_id = s.session_id) as mouse_count,
                (SELECT COUNT(*) FROM screenshots sc WHERE sc.session_id = s.session_id) as screenshot_count
            FROM sessions s
            ORDER BY s.start_time DESC
        `).all();
        res.json(rows);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.get('/api/sessions/:id', (req, res) => {
    try {
        const db = getDb();
        const session = db.prepare('SELECT * FROM sessions WHERE session_id = ?').get(req.params.id);
        if (!session) {
            return res.status(404).json({ error: 'Session not found' });
        }
        res.json(session);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.get('/api/sessions/:id/keyboard', (req, res) => {
    try {
        const db = getDb();
        const limit = parseInt(req.query.limit) || 500;
        const offset = parseInt(req.query.offset) || 0;
        const rows = db.prepare(`
            SELECT * FROM keyboard_events
            WHERE session_id = ?
            ORDER BY timestamp ASC
            LIMIT ? OFFSET ?
        `).all(req.params.id, limit, offset);
        res.json(rows);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.get('/api/sessions/:id/mouse', (req, res) => {
    try {
        const db = getDb();
        const limit = parseInt(req.query.limit) || 500;
        const offset = parseInt(req.query.offset) || 0;
        const rows = db.prepare(`
            SELECT * FROM mouse_events
            WHERE session_id = ?
            ORDER BY timestamp ASC
            LIMIT ? OFFSET ?
        `).all(req.params.id, limit, offset);
        res.json(rows);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.get('/api/sessions/:id/screenshots', (req, res) => {
    try {
        const db = getDb();
        const rows = db.prepare(`
            SELECT * FROM screenshots
            WHERE session_id = ?
            ORDER BY timestamp ASC
        `).all(req.params.id);

        const screenshots = rows.map(r => {
            const relativePath = path.relative(RECORDINGS_DIR, r.file_path).replace(/\\/g, '/');
            return {
                ...r,
                url: '/recordings/' + relativePath
            };
        });

        res.json(screenshots);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.get('/api/events/:id', (req, res) => {
    try {
        const db = getDb();
        const limit = parseInt(req.query.limit) || 1000;
        const offset = parseInt(req.query.offset) || 0;

        const rows = db.prepare(`
            SELECT 'keyboard' as type, id, session_id, timestamp, key_code as detail, pressed,
                   NULL as x, NULL as y, NULL as button_mask
            FROM keyboard_events WHERE session_id = ?
            UNION ALL
            SELECT 'mouse' as type, id, session_id, timestamp, NULL as detail, NULL as pressed,
                   x, y, button_mask
            FROM mouse_events WHERE session_id = ?
            ORDER BY timestamp ASC
            LIMIT ? OFFSET ?
        `).all(req.params.id, req.params.id, limit, offset);

        res.json(rows);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.get('/api/stats', (req, res) => {
    try {
        const db = getDb();
        const stats = db.prepare(`
            SELECT
                (SELECT COUNT(*) FROM sessions) as total_sessions,
                (SELECT COUNT(*) FROM keyboard_events) as total_key_events,
                (SELECT COUNT(*) FROM mouse_events) as total_mouse_events,
                (SELECT COUNT(*) FROM screenshots) as total_screenshots
        `).get();
        res.json(stats);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.listen(PORT, () => {
    console.log(`VNC Audit Web Interface running on http://localhost:${PORT}`);
    console.log(`Database: ${DB_PATH}`);
    console.log(`Recordings: ${RECORDINGS_DIR}`);
});
