const express = require('express');
const http = require('http');
const cors = require('cors');
const { WebSocketServer } = require('ws');

const PORT = process.env.PORT || 4000;

const app = express();
app.use(cors());
app.use(express.json());

const server = http.createServer(app);
const wss = new WebSocketServer({ server, path: '/ws' });

const nodes = new Map();

function broadcast(msg) {
  const data = JSON.stringify(msg);
  for (const ws of wss.clients) {
    if (ws.readyState === ws.OPEN) ws.send(data);
  }
}

function nodeList() {
  return Array.from(nodes.values()).map((n) => ({
    id: n.id,
    role: n.role,
    name: n.name,
    joinedAt: n.joinedAt,
  }));
}

function sendTo(ws, msg) {
  if (ws && ws.readyState === ws.OPEN) {
    ws.send(JSON.stringify(msg));
  }
}

wss.on('connection', (ws) => {
  ws.nodeId = null;

  ws.on('message', (raw) => {
    let msg;
    try {
      msg = JSON.parse(raw.toString());
    } catch (e) {
      return;
    }

    const { type, payload } = msg;

    switch (type) {
      case 'register': {
        const { id, role, name } = payload || {};
        if (!id || !role) return;
        nodes.set(id, {
          id,
          role,
          name: name || `${role}-${id.slice(0, 6)}`,
          joinedAt: Date.now(),
          ws,
        });
        ws.nodeId = id;
        sendTo(ws, { type: 'registered', payload: { id, role } });
        broadcast({ type: 'nodes:update', payload: nodeList() });
        break;
      }

      case 'unregister': {
        if (ws.nodeId && nodes.has(ws.nodeId)) {
          nodes.delete(ws.nodeId);
          broadcast({ type: 'nodes:update', payload: nodeList() });
        }
        break;
      }

      case 'nodes:list': {
        sendTo(ws, { type: 'nodes:update', payload: nodeList() });
        break;
      }

      case 'signal': {
        const { to, data } = payload || {};
        if (!to) return;
        const target = nodes.get(to);
        if (target) {
          sendTo(target.ws, {
            type: 'signal',
            payload: { from: ws.nodeId, data },
          });
        }
        break;
      }

      case 'task:announce': {
        broadcast({
          type: 'task:announce',
          payload: { ...(payload || {}), from: ws.nodeId },
        });
        break;
      }

      case 'task:progress': {
        const { to } = payload || {};
        if (!to) return;
        const target = nodes.get(to);
        if (target) {
          sendTo(target.ws, {
            type: 'task:progress',
            payload: { ...payload, from: ws.nodeId },
          });
        }
        break;
      }

      case 'chat': {
        broadcast({
          type: 'chat',
          payload: { ...(payload || {}), from: ws.nodeId },
        });
        break;
      }

      default:
        break;
    }
  });

  ws.on('close', () => {
    if (ws.nodeId && nodes.has(ws.nodeId)) {
      nodes.delete(ws.nodeId);
      broadcast({ type: 'nodes:update', payload: nodeList() });
    }
  });

  sendTo(ws, { type: 'hello', payload: { serverTime: Date.now() } });
});

app.get('/nodes', (_req, res) => {
  res.json(nodeList());
});

app.get('/health', (_req, res) => {
  res.json({ ok: true, nodes: nodes.size });
});

server.listen(PORT, () => {
  console.log(`[signaling] listening on http://localhost:${PORT}`);
});
