import {
  decodeAck, decodePong, decodeStats,
  decodeState, decodeCorrection, decodeInputAck,
  encodeInput, encodeMapping, encodePing, encodePlayerPos,
  MAX_AXES, MAX_BUTTONS,
  type StateMessage, type CorrectionMessage, type InputAckMessage,
} from './codec';

export interface TransportStats {
  rtt: number;
  loss: number;
  sent: number;
  acked: number;
  p95: number;
  reorderEvents: number;
}

type Listener = (s: TransportStats) => void;
type StateListener = (s: StateMessage) => void;
type CorrectionListener = (c: CorrectionMessage) => void;
type InputAckListener = (a: InputAckMessage) => void;

export class TransportClient {
  private url: string;
  private wt: WebTransport | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private pollSeq = 0;
  private posSeq = 0;
  private pingSeq = 0;
  public playerId = 0;
  private closed = true;
  private rtts: number[] = [];
  private pendingPings = new Map<number, number>();
  private listeners = new Set<Listener>();
  private stateListeners = new Set<StateListener>();
  private correctionListeners = new Set<CorrectionListener>();
  private inputAckListeners = new Set<InputAckListener>();
  private stats: TransportStats = { rtt: 0, loss: 0, sent: 0, acked: 0, p95: 0, reorderEvents: 0 };
  private totalIn = 0;
  private lost = 0;
  private lastAck = -1;

  constructor(url: string) {
    this.url = url;
  }

  onStats(fn: Listener) {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }
  onState(fn: StateListener) {
    this.stateListeners.add(fn);
    return () => this.stateListeners.delete(fn);
  }
  onCorrection(fn: CorrectionListener) {
    this.correctionListeners.add(fn);
    return () => this.correctionListeners.delete(fn);
  }
  onInputAck(fn: InputAckListener) {
    this.inputAckListeners.add(fn);
    return () => this.inputAckListeners.delete(fn);
  }

  getStats() { return { ...this.stats }; }
  getPollSeq() { return this.pollSeq; }

  async connect() {
    if (!this.closed) return;
    this.wt = new WebTransport(this.url, { requireUnreliable: true });
    await this.wt.ready;
    this.reader = this.wt.datagrams.readable.getReader();
    this.writer = this.wt.datagrams.writable.getWriter();
    this.closed = false;
    this.recvLoop();
    this.wt.closed.then(() => this.onClosed()).catch(() => this.onClosed());
  }

  private onClosed() {
    this.closed = true;
    if (this.reader) {
      try { this.reader.cancel(); } catch { /* ignore */ }
    }
    if (this.writer) {
      try { this.writer.close(); } catch { /* ignore */ }
    }
    this.emit();
  }

  close() {
    try { this.wt?.close(); } catch { /* ignore */ }
  }

  private async recvLoop() {
    try {
      while (this.reader && !this.closed) {
        const r = await this.reader.read();
        if (r.done) break;
        this.handle(r.value);
      }
    } catch {
      /* stream closed */
    }
  }

  private handle(data: Uint8Array) {
    if (data.length === 0) return;
    switch (data[0]) {
      case 0x02: {
        const ack = decodeAck(data);
        if (ack != null) {
          this.stats.acked = Math.max(this.stats.acked, ack);
          if (this.lastAck >= 0 && ack > this.lastAck + 1) {
            this.lost += ack - this.lastAck - 1;
          }
          this.lastAck = ack;
          this.totalIn++;
          if (this.totalIn % 10 === 0) this.updateLoss();
        }
        break;
      }
      case 0x04: {
        const pong = decodePong(data);
        if (pong) {
          const start = this.pendingPings.get(pong.seq);
          if (start != null) {
            this.pendingPings.delete(pong.seq);
            const rtt = performance.now() - start;
            this.recordRtt(rtt);
          }
        }
        break;
      }
      case 0x07: {
        const st = decodeStats(data);
        if (st) {
          this.stats.reorderEvents = st.reorderEvents;
          if (st.lastRttMs > 0) {
            this.stats.rtt = st.lastRttMs;
          }
          this.stats.loss = st.totalPackets > 0 ? (st.lostPackets / st.totalPackets) * 100 : 0;
          this.stats.acked = st.receivedSeq;
          this.emit();
        }
        break;
      }
      case 0x08: {
        const st = decodeState(data);
        if (st) {
          for (const l of this.stateListeners) l(st);
        }
        break;
      }
      case 0x09: {
        const c = decodeCorrection(data);
        if (c) {
          for (const l of this.correctionListeners) l(c);
        }
        break;
      }
      case 0x0A: {
        const a = decodeInputAck(data);
        if (a) {
          for (const l of this.inputAckListeners) l(a);
        }
        break;
      }
    }
  }

  private recordRtt(rtt: number) {
    this.rtts.push(rtt);
    if (this.rtts.length > 200) this.rtts.shift();
    this.stats.rtt = this.rtts.reduce((a, b) => a + b, 0) / this.rtts.length;
    const sorted = [...this.rtts].sort((a, b) => a - b);
    const idx = Math.floor(sorted.length * 0.95);
    this.stats.p95 = sorted[Math.max(0, idx - 1)] ?? 0;
    this.emit();
  }

  private updateLoss() {
    const total = this.stats.sent;
    const lostEst = Math.max(0, total - this.stats.acked) + this.lost;
    this.stats.loss = total > 0 ? (lostEst / total) * 100 : 0;
    this.emit();
  }

  private emit() {
    for (const l of this.listeners) l({ ...this.stats });
  }

  sendInput(padIndex: number, buttons: boolean[], axes: number[]) {
    if (!this.writer || this.closed) return;
    this.pollSeq = (this.pollSeq + 1) & 0xffff;
    const ts = Math.floor(performance.now());
    const pad = Array.from({ length: MAX_BUTTONS }, (_, i) => !!buttons[i]);
    const ax = Array.from({ length: MAX_AXES }, (_, i) => axes[i] ?? 0);
    const data = encodeInput(this.pollSeq, ts, padIndex, pad, ax);
    try {
      this.writer.write(data);
      this.stats.sent = (this.stats.sent & ~0xffff) | this.pollSeq;
      if (this.pollSeq % 20 === 0) this.updateLoss();
    } catch { /* ignore */ }
  }

  sendPing(padding = 0) {
    if (!this.writer || this.closed) return;
    this.pingSeq++;
    const ts = Math.floor(performance.now());
    this.pendingPings.set(this.pingSeq, performance.now());
    const data = encodePing(this.pingSeq, ts, padding);
    try { this.writer.write(data); } catch { /* ignore */ }
  }

  sendMapping(layout: string) {
    if (!this.writer || this.closed) return;
    try { this.writer.write(encodeMapping(layout)); } catch { /* ignore */ }
  }

  sendPlayerPos(px: number, py: number) {
    if (!this.writer || this.closed) return;
    this.posSeq = (this.posSeq + 1) & 0xffff;
    const ts = Math.floor(performance.now());
    try {
      const data = encodePlayerPos(this.posSeq, this.playerId, px, py, ts);
      this.writer.write(data);
    } catch { /* ignore */ }
  }
}
