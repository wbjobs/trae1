import { SignalingClient } from './signaling.js';
import { createPeerConnection, createDataChannel, sendDC } from './webrtc.js';
import { saveCheckpoint, loadWorkerCheckpoints, deleteCheckpoint, ckptKey } from './idb.js';

const CHUNK_TIMEOUT_MS = 30_000;

export class MasterNode {
  constructor({ signalingUrl, id, name, chunkTimeoutMs = CHUNK_TIMEOUT_MS }) {
    this.id = id;
    this.name = name;
    this.chunkTimeoutMs = chunkTimeoutMs;
    this.signaling = new SignalingClient(signalingUrl, {
      id, role: 'master', name,
    });
    this.peers = new Map();
    this.listeners = new Map();
    this.chunkTimers = new Map();
    this.chunkTasks = new Map();
    this.failures = new Map();
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

  _recordFailure(workerId, reason) {
    if (!this.failures.has(workerId)) {
      this.failures.set(workerId, { workerId, timeouts: 0, disconnects: 0, errors: 0, firstFailureAt: Date.now() });
    }
    const f = this.failures.get(workerId);
    if (reason === 'timeout') f.timeouts++;
    else if (reason === 'disconnect') f.disconnects++;
    else if (reason === 'error') f.errors++;
    f.lastFailureAt = Date.now();
    f.lastReason = reason;
  }

  _bindSignaling() {
    this.signaling.on('signal', ({ from, data }) => {
      if (!this.peers.has(from)) return;
      const { pc } = this.peers.get(from);
      this._handleSignal(pc, from, data);
    });

    this.signaling.on('nodes:update', (nodes) => {
      this.emit('nodes:update', nodes);
    });

    this.signaling.on('status', (s) => this.emit('status', s));
  }

  start() {
    this.signaling.connect();
  }

  stop() {
    for (const [id, { pc }] of this.peers) {
      try { pc.close(); } catch {}
    }
    this.peers.clear();
    for (const timerId of this.chunkTimers.values()) clearTimeout(timerId);
    this.chunkTimers.clear();
    this.chunkTasks.clear();
    this.signaling.disconnect();
  }

  getWorkers() {
    return Array.from(this.peers.values()).map((p) => ({
      id: p.id,
      name: p.name,
      state: p.pc.connectionState,
      channelState: p.dc?.readyState,
      status: p.status,
      currentChunkId: p.currentChunkId,
      startedAt: p.startedAt,
    }));
  }

  getFailures() {
    return Array.from(this.failures.values());
  }

  resetFailures() {
    this.failures.clear();
  }

  async connectToWorker(workerId, workerName) {
    if (this.peers.has(workerId)) return this.peers.get(workerId);
    const pc = createPeerConnection();
    const dc = createDataChannel(pc, 'mesh');

    const entry = {
      id: workerId,
      name: workerName,
      pc,
      dc,
      status: 'idle',
      currentChunkId: null,
      startedAt: null,
      pendingCallbacks: new Map(),
    };

    this.peers.set(workerId, entry);

    pc.onicecandidate = (ev) => {
      if (ev.candidate) {
        this.signaling.signal(workerId, { candidate: ev.candidate.toJSON() });
      }
    };
    pc.onconnectionstatechange = () => {
      this.emit('peer:state', { id: workerId, state: pc.connectionState });
      if (pc.connectionState === 'failed' || pc.connectionState === 'closed') {
        const lostChunkId = entry.currentChunkId;
        if (lostChunkId) {
          this._clearChunkTimer(lostChunkId);
          entry.status = 'idle';
          entry.currentChunkId = null;
          entry.startedAt = null;
          this._recordFailure(workerId, 'disconnect');
          this.emit('chunk:timeout', {
            workerId,
            chunkId: lostChunkId,
            reason: 'disconnect',
          });
        }
        this.peers.delete(workerId);
      }
    };

    dc.onopen = () => {
      this.emit('peer:state', { id: workerId, state: 'open' });
    };
    dc.onmessage = (ev) => this._handleData(workerId, ev.data);
    dc.onclose = () => {
      const lostChunkId = entry.currentChunkId;
      if (this.peers.has(workerId)) entry.status = 'idle';
      if (lostChunkId) {
        this._clearChunkTimer(lostChunkId);
        entry.currentChunkId = null;
        entry.startedAt = null;
        this._recordFailure(workerId, 'disconnect');
        this.emit('chunk:timeout', {
          workerId,
          chunkId: lostChunkId,
          reason: 'disconnect',
        });
      }
      this.emit('peer:state', { id: workerId, state: 'closed' });
    };
    dc.onerror = (e) => console.warn('dc error', e);

    try {
      const offer = await pc.createOffer();
      await pc.setLocalDescription(offer);
      this.signaling.signal(workerId, { desc: pc.localDescription.toJSON() });
    } catch (e) {
      console.error('offer failed', e);
      this.peers.delete(workerId);
    }

    return entry;
  }

  async _handleSignal(pc, peerId, data) {
    try {
      if (data.desc) {
        await pc.setRemoteDescription(new RTCSessionDescription(data.desc));
        if (data.desc.type === 'offer') {
          const answer = await pc.createAnswer();
          await pc.setLocalDescription(answer);
          this.signaling.signal(peerId, { desc: pc.localDescription.toJSON() });
        }
      } else if (data.candidate) {
        await pc.addIceCandidate(new RTCIceCandidate(data.candidate));
      }
    } catch (e) {
      console.warn('signal error', e);
    }
  }

  _handleData(workerId, raw) {
    let msg;
    try { msg = JSON.parse(raw); } catch { return; }
    const entry = this.peers.get(workerId);
    if (!entry) return;

    if (msg.type === 'chunk:result') {
      const matchesCurrentChunk = entry.currentChunkId === msg.chunkId;
      this._clearChunkTimer(msg.chunkId);
      deleteCheckpoint(ckptKey(workerId, msg.chunkId));
      if (!matchesCurrentChunk) return;
      entry.status = 'idle';
      entry.currentChunkId = null;
      entry.startedAt = null;
      if (msg.error) {
        this._recordFailure(workerId, 'error');
        this.emit('chunk:timeout', {
          workerId,
          chunkId: msg.chunkId,
          reason: 'error',
          error: msg.error,
        });
      } else {
        this.emit('chunk:result', {
          workerId,
          chunkId: msg.chunkId,
          result: msg.result,
          duration: msg.duration,
          error: null,
          receivedAt: Date.now(),
        });
      }
    } else if (msg.type === 'chunk:progress') {
      if (entry.currentChunkId !== msg.chunkId) return;
      const taskInfo = this.chunkTasks.get(msg.chunkId);
      if (taskInfo && msg.checkpoint) {
        saveCheckpoint(ckptKey(workerId, msg.chunkId), {
          workerId,
          taskId: taskInfo.taskId,
          taskType: taskInfo.taskType,
          chunk: taskInfo.chunk,
          checkpoint: msg.checkpoint,
        });
      }
      this.emit('chunk:progress', {
        workerId,
        chunkId: msg.chunkId,
        progress: msg.progress,
        checkpoint: msg.checkpoint,
      });
    } else if (msg.type === 'hello') {
      entry.name = msg.name || entry.name;
      sendDC(entry.dc, { type: 'welcome', name: this.name });
      this._checkResume(workerId);
    }
  }

  async _checkResume(workerId) {
    const entry = this.peers.get(workerId);
    if (!entry) return;
    try {
      const checkpoints = await loadWorkerCheckpoints(workerId);
      if (checkpoints.length > 0) {
        for (const cp of checkpoints) {
          if (entry.dc?.readyState === 'open') {
            sendDC(entry.dc, {
              type: 'task:chunk',
              taskId: cp.taskId,
              taskType: cp.taskType,
              chunk: cp.chunk,
              checkpoint: cp.checkpoint,
            });
            entry.status = 'busy';
            entry.currentChunkId = cp.chunk.id;
            entry.startedAt = Date.now();
            this.chunkTasks.set(cp.chunk.id, {
              taskId: cp.taskId,
              taskType: cp.taskType,
              chunk: cp.chunk,
            });
            this.emit('chunk:resume', {
              workerId,
              chunkId: cp.chunk.id,
              checkpoint: cp.checkpoint,
            });
          }
        }
      }
    } catch (e) {
      console.warn('resume check failed', e);
    }
  }

  _clearChunkTimer(chunkId) {
    const timerId = this.chunkTimers.get(chunkId);
    if (timerId) {
      clearTimeout(timerId);
      this.chunkTimers.delete(chunkId);
    }
  }

  sendTaskTo(workerId, { task, chunk, checkpoint }) {
    const entry = this.peers.get(workerId);
    if (!entry || !entry.dc || entry.dc.readyState !== 'open') {
      throw new Error(`Worker ${workerId} not connected`);
    }
    entry.status = 'busy';
    entry.currentChunkId = chunk.id;
    entry.startedAt = Date.now();
    this.chunkTasks.set(chunk.id, { taskId: task.id, taskType: task.type, chunk });

    if (!checkpoint) {
      deleteCheckpoint(ckptKey(workerId, chunk.id));
    }

    this._clearChunkTimer(chunk.id);
    const timerId = setTimeout(() => {
      if (!this.peers.has(workerId)) return;
      const e = this.peers.get(workerId);
      if (e.currentChunkId !== chunk.id) return;
      e.status = 'idle';
      e.currentChunkId = null;
      e.startedAt = null;
      this.chunkTimers.delete(chunk.id);
      this._recordFailure(workerId, 'timeout');
      this.emit('chunk:timeout', {
        workerId,
        chunkId: chunk.id,
        reason: 'timeout',
      });
    }, this.chunkTimeoutMs);
    this.chunkTimers.set(chunk.id, timerId);

    sendDC(entry.dc, {
      type: 'task:chunk',
      taskId: task.id,
      taskType: task.type,
      chunk,
      ...(checkpoint ? { checkpoint } : {}),
    });
  }

  hasWorker(workerId) {
    const e = this.peers.get(workerId);
    return !!e && e.dc && e.dc.readyState === 'open';
  }

  isWorkerIdle(workerId) {
    const e = this.peers.get(workerId);
    return !!e && e.dc?.readyState === 'open' && e.status === 'idle';
  }
}
