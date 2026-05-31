import { SignalingClient } from './signaling.js';
import { createPeerConnection, sendDC } from './webrtc.js';
import { createStepExecutor } from './tasks.js';

export class WorkerNode {
  constructor({ signalingUrl, id, name }) {
    this.id = id;
    this.name = name;
    this.signaling = new SignalingClient(signalingUrl, {
      id, role: 'worker', name,
    });
    this.masters = new Map();
    this.listeners = new Map();
    this.currentTask = null;
    this.currentExecutor = null;
    this.currentTimer = null;
    this._bindSignaling();
  }

  on(type, cb) {
    if (!this.listeners.has(type)) this.listeners.set(type, new Set());
    this.listeners.get(type).add(cb);
    return () => this.listeners.get(type)?.delete(cb);
  }

  emit(type, payload) {
    const set = this.listeners.get(type);
    if (set) for (const cb of set) try { cb(payload); } catch (e) { console.error(e); }
  }

  _bindSignaling() {
    this.signaling.on('signal', ({ from, data }) => this._handleSignal(from, data));
    this.signaling.on('nodes:update', (nodes) => this.emit('nodes:update', nodes));
    this.signaling.on('status', (s) => this.emit('status', s));
  }

  start() {
    this.signaling.connect();
  }

  stop() {
    this._cancelTask();
    for (const [id, { pc }] of this.masters) {
      try { pc.close(); } catch {}
    }
    this.masters.clear();
    this.signaling.disconnect();
  }

  _cancelTask() {
    if (this.currentTimer) {
      clearTimeout(this.currentTimer);
      this.currentTimer = null;
    }
    this.currentExecutor = null;
    this.currentTask = null;
  }

  _ensureMaster(masterId) {
    if (this.masters.has(masterId)) return this.masters.get(masterId);
    const pc = createPeerConnection();
    const entry = { id: masterId, pc, dc: null };

    pc.onicecandidate = (ev) => {
      if (ev.candidate) {
        this.signaling.signal(masterId, { candidate: ev.candidate.toJSON() });
      }
    };
    pc.onconnectionstatechange = () => {
      if (pc.connectionState === 'failed' || pc.connectionState === 'closed') {
        this.masters.delete(masterId);
        this.emit('master:state', { id: masterId, state: pc.connectionState });
        if (this.currentTask?.masterId === masterId) {
          this._cancelTask();
        }
      }
    };

    pc.ondatachannel = (ev) => {
      const dc = ev.channel;
      entry.dc = dc;
      dc.onopen = () => {
        sendDC(dc, { type: 'hello', name: this.name, id: this.id });
      };
      dc.onmessage = (e) => this._handleData(masterId, e.data);
      dc.onclose = () => {
        this.emit('master:state', { id: masterId, state: 'closed' });
        if (this.currentTask?.masterId === masterId) {
          this._cancelTask();
        }
      };
    };

    this.masters.set(masterId, entry);
    return entry;
  }

  async _handleSignal(masterId, data) {
    const entry = this._ensureMaster(masterId);
    const pc = entry.pc;
    try {
      if (data.desc) {
        await pc.setRemoteDescription(new RTCSessionDescription(data.desc));
        if (data.desc.type === 'offer') {
          const answer = await pc.createAnswer();
          await pc.setLocalDescription(answer);
          this.signaling.signal(masterId, { desc: pc.localDescription.toJSON() });
        }
      } else if (data.candidate) {
        await pc.addIceCandidate(new RTCIceCandidate(data.candidate));
      }
    } catch (e) {
      console.warn('signal error', e);
    }
  }

  _handleData(masterId, raw) {
    let msg;
    try { msg = JSON.parse(raw); } catch { return; }
    const entry = this.masters.get(masterId);
    if (!entry) return;

    if (msg.type === 'task:chunk') {
      this._cancelTask();
      const checkpoint = msg.checkpoint || null;
      this.currentExecutor = createStepExecutor(msg.taskType, msg.chunk, checkpoint);
      this.currentTask = {
        masterId,
        taskId: msg.taskId,
        taskType: msg.taskType,
        chunk: msg.chunk,
        startedAt: Date.now(),
        hasCheckpoint: !!checkpoint,
      };
      this.emit('task:start', {
        ...this.currentTask,
        resumed: !!checkpoint,
      });
      this._scheduleStep(entry, msg.taskId, msg.chunk.id);
    } else if (msg.type === 'task:abort') {
      if (this.currentTask?.masterId === masterId && this.currentTask?.taskId === msg.taskId) {
        this._cancelTask();
        this.emit('task:aborted', { taskId: msg.taskId });
      }
    } else if (msg.type === 'welcome') {
      this.emit('master:state', { id: masterId, state: 'open', name: msg.name });
    }
  }

  _scheduleStep(entry, taskId, chunkId) {
    this.currentTimer = setTimeout(() => {
      if (!this.currentExecutor || !this.currentTask) return;
      try {
        const result = this.currentExecutor.step();
        if (result.done) {
          const duration = performance.now() - this.currentTask.startedAt;
          const finalResult = this.currentExecutor.getResult();
          sendDC(entry.dc, {
            type: 'chunk:result',
            taskId,
            chunkId,
            result: finalResult,
            duration,
          });
          this.emit('task:done', { ...this.currentTask, duration });
          this._cancelTask();
        } else {
          const checkpoint = this.currentExecutor.getCheckpoint();
          sendDC(entry.dc, {
            type: 'chunk:progress',
            taskId,
            chunkId,
            progress: result.progress,
            checkpoint,
          });
          this.emit('task:progress', {
            ...this.currentTask,
            progress: result.progress,
          });
          this._scheduleStep(entry, taskId, chunkId);
        }
      } catch (err) {
        sendDC(entry.dc, {
          type: 'chunk:result',
          taskId,
          chunkId,
          error: String(err?.message || err),
        });
        this.emit('task:error', { ...this.currentTask, error: String(err?.message || err) });
        this._cancelTask();
      }
    }, 0);
  }
}
