import { randomId } from './utils.js';

export class SignalingClient {
  constructor(serverUrl, { id, role, name }) {
    this.serverUrl = serverUrl;
    this.id = id || randomId();
    this.role = role;
    this.name = name;
    this.ws = null;
    this.listeners = new Map();
    this.reconnectTimer = null;
    this.shouldReconnect = true;
    this.status = 'disconnected';
  }

  on(type, cb) {
    if (!this.listeners.has(type)) this.listeners.set(type, new Set());
    this.listeners.get(type).add(cb);
    return () => this.listeners.get(type)?.delete(cb);
  }

  emit(type, payload) {
    const set = this.listeners.get(type);
    if (set) {
      for (const cb of set) {
        try { cb(payload); } catch (e) { console.error(e); }
      }
    }
  }

  setStatus(s) {
    this.status = s;
    this.emit('status', s);
  }

  connect() {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) return;
    this.shouldReconnect = true;
    const proto = this.serverUrl.replace(/^http/, 'ws');
    const wsUrl = proto.endsWith('/ws') ? proto : `${proto.replace(/\/$/, '')}/ws`;
    const ws = new WebSocket(wsUrl);
    this.ws = ws;
    this.setStatus('connecting');

    ws.onopen = () => {
      this.setStatus('connected');
      this.send('register', { id: this.id, role: this.role, name: this.name });
      this.send('nodes:list');
    };

    ws.onmessage = (ev) => {
      let msg;
      try { msg = JSON.parse(ev.data); } catch { return; }
      this.emit(msg.type, msg.payload);
    };

    ws.onerror = () => this.setStatus('error');

    ws.onclose = () => {
      this.setStatus('disconnected');
      if (this.shouldReconnect) {
        clearTimeout(this.reconnectTimer);
        this.reconnectTimer = setTimeout(() => this.connect(), 1500);
      }
    };
  }

  disconnect() {
    this.shouldReconnect = false;
    clearTimeout(this.reconnectTimer);
    if (this.ws) {
      try { this.send('unregister'); } catch {}
      try { this.ws.close(); } catch {}
      this.ws = null;
    }
    this.setStatus('disconnected');
  }

  send(type, payload) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
    this.ws.send(JSON.stringify({ type, payload }));
  }

  signal(to, data) {
    this.send('signal', { to, data });
  }
}
