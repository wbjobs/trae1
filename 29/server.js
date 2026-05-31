const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const crypto = require('crypto');
const path = require('path');
const fs = require('fs');

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

const PORT = process.env.PORT || 3000;
const CHUNK_SIZE = 1 * 1024 * 1024;
const CHUNK_REQUEST_TIMEOUT = 15000;
const MIN_REPUTATION = 20;
const REPUTATION_DECAY_INTERVAL = 3600000;
const REPUTATION_DECAY_RATE = 0.95;
const REPUTATION_DATA_FILE = path.join(__dirname, 'reputation.json');

app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));
app.use('/chunks', express.static(path.join(__dirname, 'chunks')));

const nodes = new Map();
const stats = {
  totalUpload: 0,
  totalDownload: 0,
  startTime: Date.now(),
  chunkRecoveryCount: 0,
  missingChunkCount: 0
};

const fileRegistry = new Map();
const chunkHolders = new Map();
const pendingRequests = new Map();
const missingChunks = new Set();
const nodeReputations = new Map();
const reputationHistory = new Map();

const CHUNK_DIR = path.join(__dirname, 'chunks');
if (!fs.existsSync(CHUNK_DIR)) {
  fs.mkdirSync(CHUNK_DIR, { recursive: true });
}

function ensureResourceFiles() {
  const manifestPath = path.join(__dirname, 'chunks', 'manifest.json');
  if (fs.existsSync(manifestPath)) {
    const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf-8'));
    fileRegistry.set(manifest.infoHash, manifest);
    
    for (let i = 0; i < manifest.totalChunks; i++) {
      chunkHolders.set(i, new Set());
    }
    
    console.log(`[Server] Loaded resource: ${manifest.infoHash} (${manifest.chunks.length} chunks)`);
  }
}

function generateNodeId() {
  return crypto.randomBytes(20).toString('hex');
}

function getNodeList(excludeId) {
  const list = [];
  for (const [id, node] of nodes) {
    if (id !== excludeId && node.status === 'online') {
      list.push({
        id: node.id,
        ip: node.ip,
        hasChunks: node.hasChunks || [],
        uploadSpeed: node.uploadSpeed || 0,
        downloadSpeed: node.downloadSpeed || 0
      });
    }
  }
  return list;
}

function broadcastToAll(message, excludeId) {
  const data = JSON.stringify(message);
  for (const [id, node] of nodes) {
    if (id !== excludeId && node.ws && node.ws.readyState === WebSocket.OPEN) {
      node.ws.send(data);
    }
  }
}

function sendToNode(nodeId, message) {
  const node = nodes.get(nodeId);
  if (node && node.ws && node.ws.readyState === WebSocket.OPEN) {
    node.ws.send(JSON.stringify(message));
  }
}

function loadReputationData() {
  try {
    if (fs.existsSync(REPUTATION_DATA_FILE)) {
      const data = JSON.parse(fs.readFileSync(REPUTATION_DATA_FILE, 'utf-8'));
      if (data.nodes) {
        for (const [id, rep] of Object.entries(data.nodes)) {
          nodeReputations.set(id, rep);
        }
      }
      if (data.history) {
        for (const [id, history] of Object.entries(data.history)) {
          reputationHistory.set(id, history);
        }
      }
      console.log(`[Reputation] Loaded reputation data for ${nodeReputations.size} nodes`);
    }
  } catch (err) {
    console.error(`[Reputation] Error loading reputation data: ${err.message}`);
  }
}

function saveReputationData() {
  try {
    const data = {
      nodes: Object.fromEntries(nodeReputations),
      history: Object.fromEntries(reputationHistory),
      updatedAt: new Date().toISOString()
    };
    fs.writeFileSync(REPUTATION_DATA_FILE, JSON.stringify(data, null, 2), 'utf-8');
  } catch (err) {
    console.error(`[Reputation] Error saving reputation data: ${err.message}`);
  }
}

function calculateReputation(nodeId, uploaded, downloaded) {
  if (downloaded === 0) {
    return uploaded > 0 ? 100 : 50;
  }

  const ratio = uploaded / downloaded;
  let reputation;

  if (ratio >= 2) {
    reputation = 100;
  } else if (ratio >= 1.5) {
    reputation = 90;
  } else if (ratio >= 1.2) {
    reputation = 80;
  } else if (ratio >= 1.0) {
    reputation = 70;
  } else if (ratio >= 0.8) {
    reputation = 60;
  } else if (ratio >= 0.6) {
    reputation = 50;
  } else if (ratio >= 0.4) {
    reputation = 40;
  } else if (ratio >= 0.2) {
    reputation = 30;
  } else if (ratio >= 0.1) {
    reputation = 20;
  } else {
    reputation = Math.max(5, Math.floor(ratio * 100));
  }

  return reputation;
}

function updateNodeReputation(nodeId) {
  const node = nodes.get(nodeId);
  if (!node) return null;

  const oldReputation = nodeReputations.get(nodeId) || 50;
  const newReputation = calculateReputation(nodeId, node.uploaded, node.downloaded);

  const history = reputationHistory.get(nodeId) || [];
  history.push({
    timestamp: Date.now(),
    uploaded: node.uploaded,
    downloaded: node.downloaded,
    reputation: newReputation
  });

  if (history.length > 100) {
    history.shift();
  }
  reputationHistory.set(nodeId, history);

  if (newReputation !== oldReputation) {
    nodeReputations.set(nodeId, newReputation);
    console.log(`[Reputation] Node ${nodeId.substring(0, 8)}... reputation: ${oldReputation} → ${newReputation}`);

    if (newReputation < MIN_REPUTATION && oldReputation >= MIN_REPUTATION) {
      sendToNode(nodeId, {
        type: 'reputation_warning',
        reputation: newReputation,
        threshold: MIN_REPUTATION,
        message: '您的信誉分过低，将只能从源站下载'
      });
      console.log(`[Reputation] Node ${nodeId.substring(0, 8)}... restricted to source-only downloads`);
    } else if (newReputation >= MIN_REPUTATION && oldReputation < MIN_REPUTATION) {
      sendToNode(nodeId, {
        type: 'reputation_restored',
        reputation: newReputation,
        message: '您的信誉分已恢复，可享受P2P加速'
      });
      console.log(`[Reputation] Node ${nodeId.substring(0, 8)}... P2P access restored`);
    }
  }

  return newReputation;
}

function getNodeReputation(nodeId) {
  return nodeReputations.get(nodeId) || 50;
}

function isNodeRestricted(nodeId) {
  return getNodeReputation(nodeId) < MIN_REPUTATION;
}

function getReputationLeaderboard(limit = 10) {
  const leaderboard = [];

  for (const [id, reputation] of nodeReputations) {
    const node = nodes.get(id);
    leaderboard.push({
      nodeId: id,
      reputation: reputation,
      uploaded: node ? node.uploaded : 0,
      downloaded: node ? node.downloaded : 0,
      ip: node ? node.ip : 'unknown',
      isOnline: !!node,
      hasChunks: node ? node.hasChunks.length : 0
    });
  }

  leaderboard.sort((a, b) => b.reputation - a.reputation);
  return leaderboard.slice(0, limit);
}

function decayAllReputations() {
  console.log('[Reputation] Running reputation decay...');

  for (const [nodeId, reputation] of nodeReputations) {
    const decayed = Math.max(0, Math.floor(reputation * REPUTATION_DECAY_RATE));
    nodeReputations.set(nodeId, decayed);
  }

  saveReputationData();
  console.log(`[Reputation] Decayed ${nodeReputations.size} nodes' reputations`);
}

function addChunkHolder(chunkIndex, nodeId) {
  if (!chunkHolders.has(chunkIndex)) {
    chunkHolders.set(chunkIndex, new Set());
  }
  chunkHolders.get(chunkIndex).add(nodeId);
  
  if (missingChunks.has(chunkIndex)) {
    missingChunks.delete(chunkIndex);
    stats.missingChunkCount = missingChunks.size;
    console.log(`[Tracker] Chunk ${chunkIndex} is now available again via node ${nodeId.substring(0, 8)}...`);
    
    broadcastToAll({
      type: 'chunk_recovered',
      chunkIndex: chunkIndex,
      nodeId: nodeId
    });
  }
}

function removeChunkHolder(chunkIndex, nodeId) {
  const holders = chunkHolders.get(chunkIndex);
  if (holders) {
    holders.delete(nodeId);
    
    if (holders.size === 0 && chunkIndex < getTotalChunks()) {
      console.log(`[Tracker] WARNING: Chunk ${chunkIndex} has no holders! Triggering recovery...`);
      missingChunks.add(chunkIndex);
      stats.missingChunkCount = missingChunks.size;
      stats.chunkRecoveryCount++;
      
      broadcastToAll({
        type: 'chunk_missing',
        chunkIndex: chunkIndex,
        reason: 'all_holders_offline'
      });
      
      triggerChunkRecovery(chunkIndex);
    }
  }
}

function getTotalChunks() {
  for (const manifest of fileRegistry.values()) {
    return manifest.totalChunks;
  }
  return 0;
}

function triggerChunkRecovery(chunkIndex) {
  console.log(`[Tracker] Attempting to recover chunk ${chunkIndex} from source...`);
  
  for (const [manifestInfoHash, manifest] of fileRegistry) {
    const chunkPath = path.join(CHUNK_DIR, `${manifestInfoHash}_${chunkIndex}`);
    
    if (fs.existsSync(chunkPath)) {
      const chunkData = fs.readFileSync(chunkPath);
      const base64Data = chunkData.toString('base64');
      
      broadcastToAll({
        type: 'chunk_source_broadcast',
        chunkIndex: chunkIndex,
        infoHash: manifestInfoHash,
        data: base64Data,
        hash: manifest.chunks[chunkIndex].hash
      });
      
      console.log(`[Tracker] Chunk ${chunkIndex} broadcasted from source to all nodes`);
      break;
    }
  }
}

function cleanupPendingRequests(nodeId) {
  for (const [key, request] of pendingRequests) {
    if (request.targetId === nodeId) {
      pendingRequests.delete(key);
      
      sendToNode(request.fromId, {
        type: 'request_timeout',
        chunkIndex: request.chunkIndex,
        targetId: nodeId,
        reason: 'target_node_offline'
      });
      
      console.log(`[Tracker] Cancelled pending request for chunk ${request.chunkIndex} from ${request.fromId.substring(0, 8)}... to ${nodeId.substring(0, 8)}...`);
    }
  }
}

function checkAndHandleMissingChunks() {
  for (const [chunkIndex, holders] of chunkHolders) {
    const activeHolders = Array.from(holders).filter(id => {
      const node = nodes.get(id);
      return node && node.status === 'online';
    });
    
    if (activeHolders.length === 0 && chunkIndex < getTotalChunks()) {
      if (!missingChunks.has(chunkIndex)) {
        console.log(`[Tracker] Detected orphan chunk ${chunkIndex}, no active holders`);
        missingChunks.add(chunkIndex);
        stats.missingChunkCount = missingChunks.size;
        
        broadcastToAll({
          type: 'chunk_missing',
          chunkIndex: chunkIndex,
          reason: 'no_active_holders'
        });
        
        setTimeout(() => triggerChunkRecovery(chunkIndex), 1000);
      }
    }
  }
}

wss.on('connection', (ws, req) => {
  const nodeId = generateNodeId();
  const clientIp = req.socket.remoteAddress;

  const reputation = nodeReputations.get(nodeId) || 50;

  const node = {
    id: nodeId,
    ip: clientIp,
    ws: ws,
    status: 'online',
    connectedAt: Date.now(),
    hasChunks: [],
    uploadSpeed: 0,
    downloadSpeed: 0,
    uploaded: 0,
    downloaded: 0,
    reputation: reputation
  };

  nodes.set(nodeId, node);
  console.log(`[Tracker] Node connected: ${nodeId} from ${clientIp} (reputation: ${reputation})`);

  ws.send(JSON.stringify({
    type: 'welcome',
    nodeId: nodeId,
    resources: Array.from(fileRegistry.values()),
    missingChunks: Array.from(missingChunks),
    reputation: reputation,
    minReputation: MIN_REPUTATION,
    isRestricted: reputation < MIN_REPUTATION
  }));

  broadcastToAll({
    type: 'node_joined',
    node: {
      id: nodeId,
      ip: clientIp,
      hasChunks: [],
      reputation: reputation
    }
  }, nodeId);

  ws.on('message', (data) => {
    try {
      const message = JSON.parse(data.toString());
      handleMessage(nodeId, message);
    } catch (err) {
      console.error(`[Tracker] Message parse error: ${err.message}`);
    }
  });

  ws.on('close', () => {
    console.log(`[Tracker] Node disconnected: ${nodeId}`);
    
    for (const chunkIndex of node.hasChunks) {
      removeChunkHolder(chunkIndex, nodeId);
    }
    
    cleanupPendingRequests(nodeId);
    
    nodes.delete(nodeId);
    
    broadcastToAll({
      type: 'node_left',
      nodeId: nodeId
    });
    
    setTimeout(checkAndHandleMissingChunks, 2000);
  });

  ws.on('error', (err) => {
    console.error(`[Tracker] Node error ${nodeId}: ${err.message}`);
    
    for (const chunkIndex of node.hasChunks) {
      removeChunkHolder(chunkIndex, nodeId);
    }
    
    cleanupPendingRequests(nodeId);
    
    nodes.delete(nodeId);
  });
});

function handleMessage(nodeId, message) {
  const node = nodes.get(nodeId);
  if (!node) return;

  switch (message.type) {
    case 'announce':
      const newChunks = message.hasChunks || [];
      const oldChunks = node.hasChunks || [];
      
      const addedChunks = newChunks.filter(c => !oldChunks.includes(c));
      const removedChunks = oldChunks.filter(c => !newChunks.includes(c));
      
      addedChunks.forEach(chunkIndex => addChunkHolder(chunkIndex, nodeId));
      removedChunks.forEach(chunkIndex => removeChunkHolder(chunkIndex, nodeId));
      
      node.hasChunks = newChunks;
      
      if (message.uploadSpeed !== undefined) {
        node.uploadSpeed = message.uploadSpeed;
      }
      if (message.downloadSpeed !== undefined) {
        node.downloadSpeed = message.downloadSpeed;
      }
      
      broadcastToAll({
        type: 'node_update',
        node: {
          id: nodeId,
          hasChunks: node.hasChunks,
          uploadSpeed: node.uploadSpeed,
          downloadSpeed: node.downloadSpeed
        }
      }, nodeId);
      break;

    case 'get_nodes':
      node.ws.send(JSON.stringify({
        type: 'node_list',
        nodes: getNodeList(nodeId),
        missingChunks: Array.from(missingChunks)
      }));
      break;

    case 'chunk_announce':
      if (message.chunkIndex !== undefined && !node.hasChunks.includes(message.chunkIndex)) {
        node.hasChunks.push(message.chunkIndex);
        addChunkHolder(message.chunkIndex, nodeId);
      }
      broadcastToAll({
        type: 'chunk_available',
        nodeId: nodeId,
        chunkIndex: message.chunkIndex
      }, nodeId);
      break;

    case 'upload_report':
      if (message.bytes) {
        node.uploaded += message.bytes;
        stats.totalUpload += message.bytes;
        const newReputation = updateNodeReputation(nodeId);
        broadcastToAll({
          type: 'stats_update',
          stats: getPublicStats()
        });
        if (newReputation !== undefined) {
          node.reputation = newReputation;
          broadcastToAll({
            type: 'node_reputation_update',
            nodeId: nodeId,
            reputation: newReputation
          }, nodeId);
        }
      }
      break;

    case 'download_report':
      if (message.bytes) {
        node.downloaded += message.bytes;
        stats.totalDownload += message.bytes;
        const newReputation = updateNodeReputation(nodeId);
        broadcastToAll({
          type: 'stats_update',
          stats: getPublicStats()
        });
        if (newReputation !== undefined) {
          node.reputation = newReputation;
          broadcastToAll({
            type: 'node_reputation_update',
            nodeId: nodeId,
            reputation: newReputation
          }, nodeId);
        }
      }
      break;

    case 'request_chunk':
      if (isNodeRestricted(nodeId)) {
        sendToNode(nodeId, {
          type: 'request_denied',
          chunkIndex: message.chunkIndex,
          reason: 'low_reputation',
          message: '信誉分过低，只能从源站下载'
        });
        console.log(`[Tracker] Denied P2P request from low reputation node ${nodeId.substring(0, 8)}...`);
        break;
      }

      const targetNode = nodes.get(message.targetId);
      if (targetNode && targetNode.ws.readyState === WebSocket.OPEN) {
        const requestKey = `${message.fromId}_${message.chunkIndex}_${Date.now()}`;
        
        pendingRequests.set(requestKey, {
          fromId: nodeId,
          targetId: message.targetId,
          chunkIndex: message.chunkIndex,
          infoHash: message.infoHash,
          timestamp: Date.now()
        });
        
        setTimeout(() => {
          if (pendingRequests.has(requestKey)) {
            pendingRequests.delete(requestKey);
            sendToNode(nodeId, {
              type: 'request_timeout',
              chunkIndex: message.chunkIndex,
              targetId: message.targetId,
              reason: 'timeout'
            });
            console.log(`[Tracker] Request timeout for chunk ${message.chunkIndex} from ${nodeId.substring(0, 8)}... to ${message.targetId.substring(0, 8)}...`);
          }
        }, CHUNK_REQUEST_TIMEOUT);
        
        targetNode.ws.send(JSON.stringify({
          type: 'chunk_request',
          fromId: nodeId,
          chunkIndex: message.chunkIndex,
          infoHash: message.infoHash
        }));
      } else {
        sendToNode(nodeId, {
          type: 'request_failed',
          chunkIndex: message.chunkIndex,
          targetId: message.targetId,
          reason: 'target_unavailable'
        });
      }
      break;

    case 'chunk_response':
      const requestToRemove = [];
      for (const [key, request] of pendingRequests) {
        if (request.fromId === message.targetId && 
            request.chunkIndex === message.chunkIndex) {
          requestToRemove.push(key);
        }
      }
      requestToRemove.forEach(key => pendingRequests.delete(key));
      
      const requesterNode = nodes.get(message.targetId);
      if (requesterNode && requesterNode.ws.readyState === WebSocket.OPEN) {
        requesterNode.ws.send(JSON.stringify({
          type: 'chunk_data',
          fromId: nodeId,
          chunkIndex: message.chunkIndex,
          data: message.data
        }));
      }
      break;

    case 'chunk_received':
      if (message.chunkIndex !== undefined) {
        addChunkHolder(message.chunkIndex, nodeId);
        if (!node.hasChunks.includes(message.chunkIndex)) {
          node.hasChunks.push(message.chunkIndex);
        }
      }
      break;

    case 'webrtc_offer':
      const target = nodes.get(message.targetId);
      if (target && target.ws.readyState === WebSocket.OPEN) {
        target.ws.send(JSON.stringify({
          type: 'webrtc_offer',
          fromId: nodeId,
          offer: message.offer
        }));
      }
      break;

    case 'webrtc_answer':
      const answerTarget = nodes.get(message.targetId);
      if (answerTarget && answerTarget.ws.readyState === WebSocket.OPEN) {
        answerTarget.ws.send(JSON.stringify({
          type: 'webrtc_answer',
          fromId: nodeId,
          answer: message.answer
        }));
      }
      break;

    case 'webrtc_ice':
      const iceTarget = nodes.get(message.targetId);
      if (iceTarget && iceTarget.ws.readyState === WebSocket.OPEN) {
        iceTarget.ws.send(JSON.stringify({
          type: 'webrtc_ice',
          fromId: nodeId,
          candidate: message.candidate
        }));
      }
      break;
  }
}

function getPublicStats() {
  const onlineNodes = Array.from(nodes.values()).filter(n => n.status === 'online');
  const chunkAvailability = [];
  
  for (const [chunkIndex, holders] of chunkHolders) {
    const activeHolders = Array.from(holders).filter(id => {
      const node = nodes.get(id);
      return node && node.status === 'online';
    });
    chunkAvailability.push({
      index: chunkIndex,
      holders: activeHolders.length,
      isMissing: activeHolders.length === 0
    });
  }
  
  const avgReputation = onlineNodes.length > 0 
    ? Math.round(onlineNodes.reduce((sum, n) => sum + (n.reputation || 50), 0) / onlineNodes.length)
    : 0;
  
  return {
    onlineCount: onlineNodes.length,
    totalUpload: stats.totalUpload,
    totalDownload: stats.totalDownload,
    uptime: Date.now() - stats.startTime,
    chunkRecoveryCount: stats.chunkRecoveryCount,
    missingChunkCount: stats.missingChunkCount,
    missingChunks: Array.from(missingChunks),
    chunkAvailability: chunkAvailability,
    pendingRequests: pendingRequests.size,
    avgReputation: avgReputation,
    totalNodesWithReputation: nodeReputations.size,
    nodes: onlineNodes.map(n => ({
      id: n.id,
      ip: n.ip,
      hasChunks: n.hasChunks.length,
      uploaded: n.uploaded,
      downloaded: n.downloaded,
      uploadSpeed: n.uploadSpeed,
      downloadSpeed: n.downloadSpeed,
      connectedAt: n.connectedAt,
      reputation: n.reputation || 50,
      isRestricted: (n.reputation || 50) < MIN_REPUTATION
    }))
  };
}

app.get('/api/stats', (req, res) => {
  res.json(getPublicStats());
});

app.get('/api/resources', (req, res) => {
  res.json(Array.from(fileRegistry.values()));
});

app.get('/api/chunks/:infoHash/:index', (req, res) => {
  const { infoHash, index } = req.params;
  const manifest = fileRegistry.get(infoHash);

  if (!manifest) {
    return res.status(404).json({ error: 'Resource not found' });
  }

  const chunkIndex = parseInt(index);
  if (chunkIndex < 0 || chunkIndex >= manifest.chunks.length) {
    return res.status(404).json({ error: 'Invalid chunk index' });
  }

  const chunkPath = path.join(CHUNK_DIR, `${infoHash}_${chunkIndex}`);
  if (fs.existsSync(chunkPath)) {
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('X-Chunk-Hash', manifest.chunks[chunkIndex].hash);
    fs.createReadStream(chunkPath).pipe(res);
  } else {
    res.status(404).json({ error: 'Chunk not found' });
  }
});

app.get('/api/manifest/:infoHash', (req, res) => {
  const manifest = fileRegistry.get(req.params.infoHash);
  if (manifest) {
    res.json(manifest);
  } else {
    res.status(404).json({ error: 'Manifest not found' });
  }
});

app.get('/api/chunk-holders', (req, res) => {
  const result = [];
  for (const [chunkIndex, holders] of chunkHolders) {
    const activeHolders = Array.from(holders).filter(id => {
      const node = nodes.get(id);
      return node && node.status === 'online';
    });
    result.push({
      chunkIndex,
      holderCount: activeHolders.length,
      holders: activeHolders
    });
  }
  res.json(result);
});

app.get('/api/reputation/leaderboard', (req, res) => {
  const limit = parseInt(req.query.limit) || 10;
  res.json(getReputationLeaderboard(limit));
});

app.get('/api/reputation/:nodeId', (req, res) => {
  const nodeId = req.params.nodeId;
  const reputation = nodeReputations.get(nodeId);
  const history = reputationHistory.get(nodeId) || [];
  
  if (reputation !== undefined) {
    res.json({
      nodeId,
      reputation,
      isRestricted: reputation < MIN_REPUTATION,
      history: history.slice(-20)
    });
  } else {
    res.status(404).json({ error: 'Node not found' });
  }
});

app.post('/api/reputation/reset/:nodeId', (req, res) => {
  const nodeId = req.params.nodeId;
  nodeReputations.set(nodeId, 50);
  reputationHistory.delete(nodeId);
  saveReputationData();
  res.json({ success: true, message: 'Reputation reset to 50' });
});

app.get('/admin', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'admin.html'));
});

setInterval(checkAndHandleMissingChunks, 30000);
setInterval(decayAllReputations, REPUTATION_DECAY_INTERVAL);
setInterval(saveReputationData, 60000);

loadReputationData();
ensureResourceFiles();

server.listen(PORT, () => {
  console.log(`[Server] P2P CDN Server running on http://localhost:${PORT}`);
  console.log(`[Server] Tracker WebSocket on ws://localhost:${PORT}`);
  console.log(`[Server] Admin panel: http://localhost:${PORT}/admin`);
  console.log(`[Server] Chunk redundancy monitor active`);
  console.log(`[Server] Reputation system active (min: ${MIN_REPUTATION})`);
});