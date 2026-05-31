const express = require('express');
const http = require('http');
const { WebSocketServer } = require('ws');
const path = require('path');
const FingerprintDatabase = require('./database');

const PORT = process.env.PORT || 8080;
const MAX_PAYLOAD_SIZE = 1024 * 1024;
const MAX_SPECTRUM_LENGTH = 4096;
const CLIENT_TIMEOUT = 30000;
const MAX_CLIENTS = 100;
const DB_PATH = path.join(__dirname, '..', 'data', 'fingerprints.db');

const app = express();

const db = new FingerprintDatabase(DB_PATH);
console.log('[Database] Initialized at:', DB_PATH);
console.log('[Database] Statistics:', db.getStatistics());

app.use(express.json({ limit: '10mb' }));
app.use(express.static(path.join(__dirname, '..', 'dist')));

app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, '..', 'dist', 'index.html'));
});

app.get('/api/health', (req, res) => {
  const stats = db.getStatistics();
  res.json({
    status: 'ok',
    clients: wss ? wss.clients.size : 0,
    uptime: process.uptime(),
    database: stats
  });
});

app.post('/api/clips', async (req, res) => {
  try {
    const { name, duration, sampleRate, fftSize, fingerprints } = req.body;

    if (!name) {
      return res.status(400).json({ error: 'Name is required' });
    }

    const clipId = db.addClip(name, duration || 0, sampleRate || 48000, fftSize || 1024);

    if (!clipId) {
      return res.status(409).json({ error: 'Clip already exists', name });
    }

    let fingerprintCount = 0;
    if (Array.isArray(fingerprints) && fingerprints.length > 0) {
      const validFingerprints = fingerprints.filter(fp => 
        fp && typeof fp.hash === 'number' && typeof fp.time === 'number'
      );
      fingerprintCount = db.addFingerprints(clipId, validFingerprints);
    }

    const clip = db.getClip(clipId);
    res.json({
      id: clipId,
      name,
      duration,
      sampleRate,
      fftSize,
      fingerprintCount,
      clip
    });
  } catch (err) {
    console.error('[API] Error creating clip:', err);
    res.status(500).json({ error: err.message });
  }
});

app.get('/api/clips', (req, res) => {
  try {
    const clips = db.getAllClips();
    res.json({ clips, count: clips.length });
  } catch (err) {
    console.error('[API] Error getting clips:', err);
    res.status(500).json({ error: err.message });
  }
});

app.get('/api/clips/:id', (req, res) => {
  try {
    const clip = db.getClip(parseInt(req.params.id));
    if (!clip) {
      return res.status(404).json({ error: 'Clip not found' });
    }

    const fingerprints = db.getClipFingerprints(clip.id);
    const matchHistory = db.getMatchHistory(clip.id);

    res.json({
      clip,
      fingerprintCount: fingerprints.length,
      matchHistory,
      sampleFingerprints: fingerprints.slice(0, 10)
    });
  } catch (err) {
    console.error('[API] Error getting clip:', err);
    res.status(500).json({ error: err.message });
  }
});

app.delete('/api/clips/:id', (req, res) => {
  try {
    const clipId = parseInt(req.params.id);
    const clip = db.getClip(clipId);
    
    if (!clip) {
      return res.status(404).json({ error: 'Clip not found' });
    }

    db.removeClip(clipId);
    res.json({ success: true, deletedClip: clip.name });
  } catch (err) {
    console.error('[API] Error deleting clip:', err);
    res.status(500).json({ error: err.message });
  }
});

app.post('/api/fingerprints/store', async (req, res) => {
  try {
    const { clipId, fingerprints } = req.body;

    if (!clipId) {
      return res.status(400).json({ error: 'clipId is required' });
    }

    const clip = db.getClip(clipId);
    if (!clip) {
      return res.status(404).json({ error: 'Clip not found' });
    }

    if (!Array.isArray(fingerprints) || fingerprints.length === 0) {
      return res.status(400).json({ error: 'Fingerprints array is required' });
    }

    const validFingerprints = fingerprints.filter(fp => 
      fp && typeof fp.hash === 'number' && typeof fp.time === 'number'
    );

    const count = db.addFingerprints(clipId, validFingerprints);
    
    res.json({
      success: true,
      stored: count,
      total: db.getClip(clipId).hash_count
    });
  } catch (err) {
    console.error('[API] Error storing fingerprints:', err);
    res.status(500).json({ error: err.message });
  }
});

app.post('/api/match', async (req, res) => {
  try {
    const { fingerprints, timeTolerance, minMatches } = req.body;

    if (!Array.isArray(fingerprints) || fingerprints.length === 0) {
      return res.status(400).json({ error: 'Fingerprints array is required' });
    }

    const validFingerprints = fingerprints.filter(fp => 
      fp && typeof fp.hash === 'number' && typeof fp.time === 'number'
    );

    if (validFingerprints.length === 0) {
      return res.status(400).json({ error: 'No valid fingerprints provided' });
    }

    const tolerance = timeTolerance || 3;
    const minMatchCount = minMatches || 5;

    console.log(`[Match] Matching ${validFingerprints.length} fingerprints with tolerance ${tolerance}s`);

    const results = db.matchFingerprints(validFingerprints, tolerance);
    
    const filteredResults = results.filter(r => r.matchCount >= minMatchCount);

    const response = {
      query: {
        fingerprintCount: validFingerprints.length,
        timeTolerance: tolerance,
        minMatches: minMatchCount
      },
      matches: filteredResults.slice(0, 20),
      totalMatches: filteredResults.length
    };

    if (filteredResults.length > 0) {
      const bestMatch = filteredResults[0];
      db.saveMatchResult(
        null,
        bestMatch.clipId,
        bestMatch.score,
        bestMatch.matchCount,
        bestMatch.averageConfidence,
        bestMatch.matches[0]?.timeDiff || 0
      );
    }

    res.json(response);
  } catch (err) {
    console.error('[API] Error matching fingerprints:', err);
    res.status(500).json({ error: err.message });
  }
});

app.get('/api/match/history', (req, res) => {
  try {
    const clipId = req.query.clipId ? parseInt(req.query.clipId) : null;
    let history;
    
    if (clipId) {
      history = db.getMatchHistory(clipId);
    } else {
      const allClips = db.getAllClips();
      history = [];
      for (const clip of allClips) {
        const clipHistory = db.getMatchHistory(clip.id);
        history.push(...clipHistory);
      }
    }

    res.json({ history, count: history.length });
  } catch (err) {
    console.error('[API] Error getting match history:', err);
    res.status(500).json({ error: err.message });
  }
});

app.get('/api/statistics', (req, res) => {
  try {
    const stats = db.getStatistics();
    const clips = db.getAllClips();
    
    res.json({
      database: stats,
      clips: clips.map(c => ({
        id: c.id,
        name: c.name,
        hashCount: c.hash_count,
        duration: c.duration,
        createdAt: c.created_at
      }))
    });
  } catch (err) {
    console.error('[API] Error getting statistics:', err);
    res.status(500).json({ error: err.message });
  }
});

const server = http.createServer(app);

const wss = new WebSocketServer({ 
  server, 
  path: '/ws',
  maxPayload: MAX_PAYLOAD_SIZE
});

const clients = new Map();
const lastSpectrumData = new Map();
const fingerprintSessions = new Map();

wss.on('connection', (ws, req) => {
  if (clients.size >= MAX_CLIENTS) {
    console.log('[WebSocket] Max clients reached, rejecting connection');
    ws.close(1013, 'Server full');
    return;
  }

  const clientId = generateClientId();
  const clientInfo = {
    id: clientId,
    ws,
    connectedAt: new Date(),
    lastActivity: Date.now(),
    role: 'viewer',
    ip: req.socket.remoteAddress,
    recordingSession: null
  };

  clients.set(clientId, clientInfo);

  console.log(`[WebSocket] Client connected: ${clientId}`);
  console.log(`[WebSocket] Total clients: ${clients.size}`);

  ws.send(JSON.stringify({
    type: 'welcome',
    clientId,
    message: 'Connected to audio spectrum server',
    maxSpectrumLength: MAX_SPECTRUM_LENGTH
  }));

  for (const [id, data] of lastSpectrumData) {
    if (id !== clientId && validateSpectrumData(data)) {
      ws.send(JSON.stringify({
        type: 'broadcast',
        sourceId: id,
        spectrum: data.spectrum,
        fftSize: data.fftSize,
        timestamp: data.timestamp
      }));
    }
  }

  const timeoutInterval = setInterval(() => {
    if (clientInfo.lastActivity && Date.now() - clientInfo.lastActivity > CLIENT_TIMEOUT) {
      console.log(`[WebSocket] Client timeout: ${clientId}`);
      ws.close(1000, 'Timeout');
      clearInterval(timeoutInterval);
    }
  }, 10000);

  ws.on('message', (data) => {
    clientInfo.lastActivity = Date.now();

    try {
      if (data.length > MAX_PAYLOAD_SIZE) {
        console.warn(`[WebSocket] Payload too large from ${clientId}: ${data.length} bytes`);
        ws.send(JSON.stringify({
          type: 'error',
          message: 'Payload too large'
        }));
        return;
      }

      const message = JSON.parse(data.toString());
      handleMessage(clientId, message);
    } catch (err) {
      console.error(`[WebSocket] Parse error from ${clientId}:`, err.message);
      ws.send(JSON.stringify({
        type: 'error',
        message: 'Invalid JSON format'
      }));
    }
  });

  ws.on('close', () => {
    console.log(`[WebSocket] Client disconnected: ${clientId}`);
    clearInterval(timeoutInterval);
    
    if (fingerprintSessions.has(clientId)) {
      const session = fingerprintSessions.get(clientId);
      console.log(`[WebSocket] Session ${clientId} collected ${session.fingerprints.length} fingerprints`);
      fingerprintSessions.delete(clientId);
    }
    
    clients.delete(clientId);
    lastSpectrumData.delete(clientId);
    broadcastClientList();
  });

  ws.on('error', (err) => {
    console.error(`[WebSocket] Error from ${clientId}:`, err.message);
    clearInterval(timeoutInterval);
    clients.delete(clientId);
    lastSpectrumData.delete(clientId);
  });

  broadcastClientList();
});

function handleMessage(clientId, message) {
  const client = clients.get(clientId);
  if (!client) return;

  switch (message.type) {
    case 'register':
      client.role = message.role || 'viewer';
      console.log(`[WebSocket] Client ${clientId} registered as: ${client.role}`);
      break;

    case 'spectrum':
      if (!message.spectrum || !Array.isArray(message.spectrum)) {
        console.warn(`[WebSocket] Invalid spectrum data from ${clientId}`);
        return;
      }

      if (message.spectrum.length > MAX_SPECTRUM_LENGTH) {
        console.warn(`[WebSocket] Spectrum too long from ${clientId}: ${message.spectrum.length}`);
        message.spectrum = message.spectrum.slice(0, MAX_SPECTRUM_LENGTH);
      }

      const validatedSpectrum = validateSpectrumArray(message.spectrum);
      if (!validatedSpectrum) {
        console.warn(`[WebSocket] Invalid spectrum values from ${clientId}`);
        return;
      }

      const spectrumData = {
        spectrum: validatedSpectrum,
        fftSize: message.fftSize || 512,
        timestamp: Date.now()
      };

      lastSpectrumData.set(clientId, spectrumData);

      broadcastToOthers(clientId, {
        type: 'broadcast',
        sourceId: clientId,
        spectrum: validatedSpectrum,
        fftSize: message.fftSize,
        timestamp: spectrumData.timestamp
      });
      break;

    case 'fingerprints':
      handleFingerprints(clientId, message, client);
      break;

    case 'start_recording':
      if (!fingerprintSessions.has(clientId)) {
        fingerprintSessions.set(clientId, {
          startTime: Date.now(),
          fingerprints: [],
          clipName: message.clipName || `Recording_${Date.now()}`
        });
        console.log(`[WebSocket] Recording started for ${clientId}: ${message.clipName}`);
      }
      client.ws.send(JSON.stringify({
        type: 'recording_started',
        sessionId: clientId
      }));
      break;

    case 'stop_recording':
      const session = fingerprintSessions.get(clientId);
      if (session) {
        session.endTime = Date.now();
        session.duration = (session.endTime - session.startTime) / 1000;
        
        const clipId = db.addClip(
          session.clipName,
          session.duration,
          48000,
          1024
        );
        
        if (clipId && session.fingerprints.length > 0) {
          db.addFingerprints(clipId, session.fingerprints);
        }
        
        console.log(`[WebSocket] Recording stopped for ${clientId}: ${session.fingerprints.length} fingerprints`);
        
        client.ws.send(JSON.stringify({
          type: 'recording_stopped',
          clipId,
          clipName: session.clipName,
          duration: session.duration,
          fingerprintCount: session.fingerprints.length
        }));
        
        fingerprintSessions.delete(clientId);
      }
      break;

    case 'match_fingerprints':
      handleWebSocketMatch(clientId, message, client);
      break;

    case 'request_clips':
      const clips = db.getAllClips();
      client.ws.send(JSON.stringify({
        type: 'clips_list',
        clips: clips.map(c => ({
          id: c.id,
          name: c.name,
          hashCount: c.hash_count,
          duration: c.duration
        }))
      }));
      break;

    case 'request_clients':
      client.ws.send(JSON.stringify({
        type: 'client_list',
        clients: Array.from(clients.keys())
      }));
      break;

    case 'ping':
      client.ws.send(JSON.stringify({
        type: 'pong',
        timestamp: Date.now()
      }));
      break;

    default:
      console.log(`[WebSocket] Unknown message type from ${clientId}: ${message.type}`);
  }
}

function handleFingerprints(clientId, message, client) {
  if (!message.fingerprints || !Array.isArray(message.fingerprints)) {
    return;
  }

  const validFingerprints = message.fingerprints.filter(fp => 
    fp && typeof fp.hash === 'number' && typeof fp.time === 'number'
  );

  if (validFingerprints.length === 0) {
    return;
  }

  const session = fingerprintSessions.get(clientId);
  if (session) {
    session.fingerprints.push(...validFingerprints);
    
    if (session.fingerprints.length % 100 === 0) {
      console.log(`[WebSocket] Session ${clientId}: ${session.fingerprints.length} fingerprints collected`);
    }
  }

  if (message.store && message.clipId) {
    const count = db.addFingerprints(message.clipId, validFingerprints);
    client.ws.send(JSON.stringify({
      type: 'fingerprints_stored',
      clipId: message.clipId,
      count
    }));
  }
}

function handleWebSocketMatch(clientId, message, client) {
  if (!message.fingerprints || !Array.isArray(message.fingerprints)) {
    client.ws.send(JSON.stringify({
      type: 'match_error',
      message: 'Fingerprints array is required'
    }));
    return;
  }

  const validFingerprints = message.fingerprints.filter(fp => 
    fp && typeof fp.hash === 'number' && typeof fp.time === 'number'
  );

  if (validFingerprints.length === 0) {
    client.ws.send(JSON.stringify({
      type: 'match_error',
      message: 'No valid fingerprints provided'
    }));
    return;
  }

  const timeTolerance = message.timeTolerance || 3;
  const results = db.matchFingerprints(validFingerprints, timeTolerance);

  client.ws.send(JSON.stringify({
    type: 'match_result',
    matches: results.slice(0, 10),
    totalMatches: results.length,
    query: {
      fingerprintCount: validFingerprints.length,
      timeTolerance
    }
  }));
}

function validateSpectrumArray(spectrum) {
  if (!Array.isArray(spectrum) || spectrum.length === 0) {
    return null;
  }

  const validated = new Array(spectrum.length);
  let hasValidValue = false;

  for (let i = 0; i < spectrum.length; i++) {
    const value = spectrum[i];
    
    if (typeof value !== 'number' || !isFinite(value)) {
      validated[i] = 0;
    } else {
      validated[i] = Math.max(0, Math.min(1, value));
      if (validated[i] > 0) {
        hasValidValue = true;
      }
    }
  }

  return hasValidValue || spectrum.length > 0 ? validated : null;
}

function validateSpectrumData(data) {
  if (!data || !data.spectrum || !Array.isArray(data.spectrum)) {
    return false;
  }
  
  if (data.spectrum.length === 0 || data.spectrum.length > MAX_SPECTRUM_LENGTH) {
    return false;
  }
  
  return true;
}

function broadcastToOthers(sourceId, message) {
  const data = JSON.stringify(message);
  
  if (data.length > MAX_PAYLOAD_SIZE) {
    console.warn('[WebSocket] Broadcast payload too large, skipping');
    return;
  }
  
  for (const [id, client] of clients) {
    if (id !== sourceId && client.ws.readyState === 1) {
      client.ws.send(data);
    }
  }
}

function broadcastClientList() {
  const clientList = Array.from(clients.entries()).map(([id, info]) => ({
    id,
    role: info.role,
    connectedAt: info.connectedAt
  }));

  const message = JSON.stringify({
    type: 'client_list',
    clients: clientList,
    count: clients.size
  });

  for (const [id, client] of clients) {
    if (client.ws.readyState === 1) {
      client.ws.send(message);
    }
  }
}

function generateClientId() {
  return 'client_' + Math.random().toString(36).substring(2, 9) + '_' + Date.now().toString(36);
}

server.listen(PORT, () => {
  console.log('========================================');
  console.log('  Audio Spectrum Server');
  console.log('========================================');
  console.log(`HTTP Server: http://localhost:${PORT}`);
  console.log(`WebSocket:  ws://localhost:${PORT}/ws`);
  console.log(`Database:   ${DB_PATH}`);
  console.log(`Max Clients: ${MAX_CLIENTS}`);
  console.log(`Max Payload: ${MAX_PAYLOAD_SIZE / 1024} KB`);
  console.log('========================================');
  console.log('REST API Endpoints:');
  console.log('  POST /api/clips           - Create clip');
  console.log('  GET  /api/clips           - List clips');
  console.log('  GET  /api/clips/:id       - Get clip details');
  console.log('  DELETE /api/clips/:id     - Delete clip');
  console.log('  POST /api/fingerprints/store - Store fingerprints');
  console.log('  POST /api/match           - Match fingerprints');
  console.log('  GET  /api/statistics      - Database statistics');
  console.log('========================================');
});

server.on('error', (err) => {
  console.error('[Server] Error:', err.message);
});

process.on('SIGINT', () => {
  console.log('\n[Server] Shutting down...');
  
  for (const [id, client] of clients) {
    client.ws.close();
  }
  
  db.close();
  server.close(() => {
    console.log('[Server] Server closed');
    process.exit(0);
  });
});

process.on('SIGTERM', () => {
  console.log('\n[Server] Shutting down...');
  
  for (const [id, client] of clients) {
    client.ws.close();
  }
  
  db.close();
  server.close(() => {
    console.log('[Server] Server closed');
    process.exit(0);
  });
});

process.on('uncaughtException', (err) => {
  console.error('[Server] Uncaught exception:', err);
});

process.on('unhandledRejection', (reason, promise) => {
  console.error('[Server] Unhandled rejection:', reason);
});
