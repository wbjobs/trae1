export const MsgType = {
  Input: 0x01,
  Ack: 0x02,
  Ping: 0x03,
  Pong: 0x04,
  Mapping: 0x05,
  Handshake: 0x06,
  Stats: 0x07,
  State: 0x08,
  Correction: 0x09,
  InputAck: 0x0A,
  PlayerPos: 0x0B,
} as const;

export const MAX_BUTTONS = 17;
export const MAX_AXES = 6;

export interface GamepadSnapshot {
  buttons: boolean[];
  axes: number[];
  ts: number;
}

function encodeUvarint(n: number): Uint8Array {
  const out: number[] = [];
  while (n > 0x7f) {
    out.push((n & 0x7f) | 0x80);
    n >>>= 7;
  }
  out.push(n);
  return new Uint8Array(out);
}

export function encodeInput(seq: number, ts: number, padIndex: number, buttons: boolean[], axes: number[]): Uint8Array {
  const parts: Uint8Array[] = [];
  parts.push(new Uint8Array([MsgType.Input]));
  parts.push(encodeUvarint(seq));
  parts.push(encodeUvarint(ts));
  parts.push(new Uint8Array([padIndex]));

  const buttonBytes = Math.ceil(MAX_BUTTONS / 8);
  const bb = new Uint8Array(buttonBytes);
  for (let i = 0; i < MAX_BUTTONS; i++) {
    if (buttons[i]) {
      bb[Math.floor(i / 8)] |= 1 << (i % 8);
    }
  }
  parts.push(bb);

  const ab = new Uint8Array(MAX_AXES * 4);
  const dv = new DataView(ab.buffer);
  for (let i = 0; i < MAX_AXES; i++) {
    dv.setFloat32(i * 4, axes[i] ?? 0, true);
  }
  parts.push(ab);

  let total = 0;
  for (const p of parts) total += p.length;
  const out = new Uint8Array(total);
  let off = 0;
  for (const p of parts) {
    out.set(p, off);
    off += p.length;
  }
  return out;
}

export function encodePing(seq: number, ts: number, padding = 0): Uint8Array {
  const out: Uint8Array[] = [];
  out.push(new Uint8Array([MsgType.Ping]));
  out.push(encodeUvarint(seq));
  out.push(encodeUvarint(ts));
  out.push(new Uint8Array([padding & 0xff, (padding >> 8) & 0xff]));
  const total = out.reduce((a, b) => a + b.length, 0);
  const buf = new Uint8Array(total);
  let off = 0;
  for (const p of out) {
    buf.set(p, off);
    off += p.length;
  }
  return buf;
}

export function encodeMapping(layout: string): Uint8Array {
  const payload = new TextEncoder().encode(JSON.stringify({ layout }));
  const out = new Uint8Array(1 + payload.length);
  out[0] = MsgType.Mapping;
  out.set(payload, 1);
  return out;
}

export function decodeAck(data: Uint8Array): number | null {
  if (data.length < 2 || data[0] !== MsgType.Ack) return null;
  let n = 0;
  let shift = 0;
  for (let i = 1; i < data.length; i++) {
    const b = data[i];
    n |= (b & 0x7f) << shift;
    shift += 7;
    if ((b & 0x80) === 0) break;
  }
  return n;
}

export function decodePong(data: Uint8Array): { seq: number; origTs: number; recvTs: number } | null {
  if (data.length < 2 || data[0] !== MsgType.Pong) return null;
  let idx = 1;
  const readVarint = () => {
    let n = 0;
    let shift = 0;
    while (idx < data.length) {
      const b = data[idx++];
      n |= (b & 0x7f) << shift;
      shift += 7;
      if ((b & 0x80) === 0) break;
    }
    return n;
  };
  const seq = readVarint();
  const origTs = readVarint();
  const recvTs = readVarint();
  return { seq, origTs, recvTs };
}

export interface StatsMessage {
  seq: number;
  receivedSeq: number;
  lastRttMs: number;
  lostPackets: number;
  totalPackets: number;
  reorderEvents: number;
}

export function decodeStats(data: Uint8Array): StatsMessage | null {
  if (data.length < 2 || data[0] !== MsgType.Stats) return null;
  let idx = 1;
  const readVarint = () => {
    let n = 0;
    let shift = 0;
    while (idx < data.length) {
      const b = data[idx++];
      n |= (b & 0x7f) << shift;
      shift += 7;
      if ((b & 0x80) === 0) break;
    }
    return n;
  };
  const seq = readVarint();
  const receivedSeq = readVarint();
  if (idx + 2 > data.length) return null;
  const lastRttMs = data[idx] | (data[idx + 1] << 8);
  idx += 2;
  const lostPackets = readVarint();
  const totalPackets = readVarint();
  const reorderEvents = readVarint();
  return { seq, receivedSeq, lastRttMs, lostPackets, totalPackets, reorderEvents };
}

export interface EntityState {
  playerId: number;
  posX: number;
  posY: number;
  velX: number;
  velY: number;
}

export interface StateMessage {
  frame: number;
  timestamp: number;
  entities: EntityState[];
}

function readFloat64(data: Uint8Array, idx: number): number {
  const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
  return dv.getFloat64(idx, true);
}

function encodeFloat64(v: number): Uint8Array {
  const buf = new Uint8Array(8);
  const dv = new DataView(buf.buffer);
  dv.setFloat64(0, v, true);
  return buf;
}

export function decodeState(data: Uint8Array): StateMessage | null {
  if (data.length < 2 || data[0] !== MsgType.State) return null;
  let idx = 1;
  const readVarint = () => {
    let n = 0;
    let shift = 0;
    while (idx < data.length) {
      const b = data[idx++];
      n |= (b & 0x7f) << shift;
      shift += 7;
      if ((b & 0x80) === 0) break;
    }
    return n;
  };
  const frame = readVarint();
  const ts = readVarint();
  const cnt = readVarint();
  const entities: EntityState[] = [];
  for (let i = 0; i < cnt; i++) {
    if (idx + 36 > data.length) break;
    const playerId = (data[idx] | (data[idx + 1] << 8) | (data[idx + 2] << 16) | (data[idx + 3] << 24)) >> 0;
    idx += 4;
    const posX = readFloat64(data, idx); idx += 8;
    const posY = readFloat64(data, idx); idx += 8;
    const velX = readFloat64(data, idx); idx += 8;
    const velY = readFloat64(data, idx); idx += 8;
    entities.push({ playerId, posX, posY, velX, velY });
  }
  return { frame, timestamp: ts, entities };
}

export interface CorrectionMessage {
  playerId: number;
  frame: number;
  correctPosX: number;
  correctPosY: number;
  correctVelX: number;
  correctVelY: number;
  yourPosX: number;
  yourPosY: number;
}

export function decodeCorrection(data: Uint8Array): CorrectionMessage | null {
  if (data.length < 6 || data[0] !== MsgType.Correction) return null;
  let idx = 1;
  const readVarint = () => {
    let n = 0;
    let shift = 0;
    while (idx < data.length) {
      const b = data[idx++];
      n |= (b & 0x7f) << shift;
      shift += 7;
      if ((b & 0x80) === 0) break;
    }
    return n;
  };
  const playerId = (data[idx] | (data[idx + 1] << 8) | (data[idx + 2] << 16) | (data[idx + 3] << 24)) >> 0;
  idx += 4;
  const frame = readVarint();
  if (idx + 48 > data.length) return null;
  const correctPosX = readFloat64(data, idx); idx += 8;
  const correctPosY = readFloat64(data, idx); idx += 8;
  const correctVelX = readFloat64(data, idx); idx += 8;
  const correctVelY = readFloat64(data, idx); idx += 8;
  const yourPosX = readFloat64(data, idx); idx += 8;
  const yourPosY = readFloat64(data, idx); idx += 8;
  return { playerId, frame, correctPosX, correctPosY, correctVelX, correctVelY, yourPosX, yourPosY };
}

export function encodePlayerPos(seq: number, playerId: number, px: number, py: number, ts: number): Uint8Array {
  const parts: Uint8Array[] = [];
  parts.push(new Uint8Array([MsgType.PlayerPos]));
  parts.push(new Uint8Array([seq & 0xff, (seq >> 8) & 0xff]));
  const pid = new Uint8Array(4);
  pid[0] = playerId & 0xff;
  pid[1] = (playerId >> 8) & 0xff;
  pid[2] = (playerId >> 16) & 0xff;
  pid[3] = (playerId >> 24) & 0xff;
  parts.push(pid);
  parts.push(encodeFloat64(px));
  parts.push(encodeFloat64(py));
  parts.push(encodeUvarint(ts));
  const total = parts.reduce((a, b) => a + b.length, 0);
  const buf = new Uint8Array(total);
  let off = 0;
  for (const p of parts) { buf.set(p, off); off += p.length; }
  return buf;
}

export interface InputAckMessage {
  seq: number;
  frame: number;
}

export function decodeInputAck(data: Uint8Array): InputAckMessage | null {
  if (data.length < 3 || data[0] !== MsgType.InputAck) return null;
  const seq = data[1] | (data[2] << 8);
  let idx = 3;
  let n = 0, shift = 0;
  while (idx < data.length) {
    const b = data[idx++];
    n |= (b & 0x7f) << shift;
    shift += 7;
    if ((b & 0x80) === 0) break;
  }
  return { seq, frame: n };
}
