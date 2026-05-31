import { World, Entity, Input, FixedTimestep, vec2 } from '../physics/world';

export interface CorrectionEvent {
  frame: number;
  predicted: { x: number; y: number };
  corrected: { x: number; y: number };
  diff: number;
}

export interface RollbackStats {
  rollbackCount: number;
  totalCorrectedFrames: number;
  avgCorrectionDistance: number;
  lastCorrectionTime: number;
}

interface InputRecord {
  frame: number;
  input: Input;
  predictedPos: { x: number; y: number };
}

interface Snapshot {
  frame: number;
  entities: Entity[];
}

interface CorrectionState {
  active: boolean;
  fromFrame: number;
  toFrame: number;
  startPos: { x: number; y: number };
  endPos: { x: number; y: number };
  progress: number;
  duration: number;
}

export class RollbackClient {
  private world: World;
  private localPlayerId: number;
  private inputHistory: InputRecord[] = [];
  private maxHistorySize = 180;
  private authorityFrame = 0;
  private localFrame = 0;
  private correctionState: CorrectionState | null = null;
  private lastAuthorityPos: { x: number; y: number } = { x: 0, y: 0 };
  private lastPredictedPos: { x: number; y: number } = { x: 0, y: 0 };
  private predictionWindowMs: number;
  private predictionFrames: number;
  private stats: RollbackStats = {
    rollbackCount: 0,
    totalCorrectedFrames: 0,
    avgCorrectionDistance: 0,
    lastCorrectionTime: 0,
  };
  private onCorrection: ((ev: CorrectionEvent) => void) | null = null;

  constructor(localPlayerId: number, predictionWindowMs = 100) {
    this.world = new World();
    this.world.addPlayer(localPlayerId);
    this.localPlayerId = localPlayerId;
    this.predictionWindowMs = predictionWindowMs;
    this.predictionFrames = Math.max(1, Math.round(predictionWindowMs / 1000 / FixedTimestep));
  }

  setOnCorrection(fn: (ev: CorrectionEvent) => void) {
    this.onCorrection = fn;
  }

  setPredictionWindow(ms: number) {
    this.predictionWindowMs = ms;
    this.predictionFrames = Math.max(1, Math.round(ms / 1000 / FixedTimestep));
  }

  getPredictionFrames() { return this.predictionFrames; }
  getAuthorityFrame() { return this.authorityFrame; }
  getLocalFrame() { return this.localFrame; }
  getStats() { return { ...this.stats }; }
  getLastAuthorityPos() { return { ...this.lastAuthorityPos }; }
  getLastPredictedPos() { return { ...this.lastPredictedPos }; }
  getWorld() { return this.world; }
  getLocalPlayerId() { return this.localPlayerId; }

  applyInput(input: Input) {
    if (this.correctionState?.active) {
      return;
    }
    const player = this.world.getPlayer(this.localPlayerId);
    const prePos = player ? { ...player.pos } : { x: 0, y: 0 };

    const inputs = new Map<number, Input>();
    inputs.set(this.localPlayerId, input);
    this.world.step(inputs);
    this.localFrame++;

    const postPos = player ? { ...player.pos } : { x: 0, y: 0 };
    this.lastPredictedPos = postPos;

    this.inputHistory.push({
      frame: this.localFrame,
      input: { ...input, buttons: [...input.buttons] },
      predictedPos: postPos,
    });

    if (this.inputHistory.length > this.maxHistorySize) {
      this.inputHistory = this.inputHistory.slice(-this.maxHistorySize);
    }
  }

  applyAuthorityState(frame: number, entity: Entity) {
    if (frame <= this.authorityFrame) return;
    this.authorityFrame = frame;
    this.lastAuthorityPos = { ...entity.pos };

    const player = this.world.getPlayer(this.localPlayerId);
    if (!player) {
      this.world.entities.push({ ...entity, playerId: this.localPlayerId });
      return;
    }

    const dx = player.pos.x - entity.pos.x;
    const dy = player.pos.y - entity.pos.y;
    const dist = Math.sqrt(dx * dx + dy * dy);

    if (dist > 0.1) {
      this.stats.rollbackCount++;
      this.stats.totalCorrectedFrames += this.localFrame - frame;
      const n = this.stats.rollbackCount;
      this.stats.avgCorrectionDistance =
        (this.stats.avgCorrectionDistance * (n - 1) + dist) / n;
      this.stats.lastCorrectionTime = Date.now();

      if (this.onCorrection) {
        this.onCorrection({
          frame,
          predicted: { x: player.pos.x, y: player.pos.y },
          corrected: { x: entity.pos.x, y: entity.pos.y },
          diff: dist,
        });
      }

      this.rollbackAndReplay(frame, entity);
    }
  }

  private rollbackAndReplay(targetFrame: number, correctEntity: Entity) {
    const startIdx = this.inputHistory.findIndex(r => r.frame >= targetFrame);
    if (startIdx < 0) {
      const player = this.world.getPlayer(this.localPlayerId);
      if (player) {
        this.startCorrection(player.pos, correctEntity.pos);
      }
      return;
    }

    const baseFrame = targetFrame - 1;
    let baseSnapshot = this.restoreSnapshot(baseFrame);
    if (!baseSnapshot) {
      baseSnapshot = {
        frame: baseFrame,
        entities: [{ ...correctEntity, playerId: this.localPlayerId }],
      };
    }

    const player = baseSnapshot.entities.find(e => e.playerId === this.localPlayerId);
    if (player) {
      player.pos = { ...correctEntity.pos };
      player.vel = { ...correctEntity.vel };
    }

    this.world.entities = baseSnapshot.entities.map(e => ({ ...e, pos: { ...e.pos }, vel: { ...e.vel } }));
    this.localFrame = baseSnapshot.frame;

    const remaining = this.inputHistory.slice(startIdx);
    for (const rec of remaining) {
      const inputs = new Map<number, Input>();
      inputs.set(this.localPlayerId, rec.input);
      this.world.step(inputs);
      this.localFrame++;
    }

    const p = this.world.getPlayer(this.localPlayerId);
    if (p) {
      this.lastPredictedPos = { ...p.pos };
    }
  }

  private restoreSnapshot(frame: number): Snapshot | null {
    const rec = this.inputHistory.find(r => r.frame === frame);
    if (!rec) return null;
    return {
      frame,
      entities: [{
        pos: { ...rec.predictedPos },
        vel: vec2(),
        radius: 20,
        playerId: this.localPlayerId,
      }],
    };
  }

  private startCorrection(from: { x: number; y: number }, to: { x: number; y: number }) {
    this.correctionState = {
      active: true,
      fromFrame: this.localFrame,
      toFrame: this.localFrame + 6,
      startPos: { ...from },
      endPos: { ...to },
      progress: 0,
      duration: 6,
    };
    const player = this.world.getPlayer(this.localPlayerId);
    if (player) {
      player.pos = { ...from };
    }
  }

  updateInterpolation(dt: number) {
    if (!this.correctionState?.active) return;
    const cs = this.correctionState;
    cs.progress += dt / (FixedTimestep * cs.duration);
    if (cs.progress >= 1) {
      cs.progress = 1;
      cs.active = false;
      const player = this.world.getPlayer(this.localPlayerId);
      if (player) {
        player.pos = { ...cs.endPos };
      }
      this.correctionState = null;
      return;
    }
    const t = this.easeOutCubic(cs.progress);
    const player = this.world.getPlayer(this.localPlayerId);
    if (player) {
      player.pos = {
        x: cs.startPos.x + (cs.endPos.x - cs.startPos.x) * t,
        y: cs.startPos.y + (cs.endPos.y - cs.startPos.y) * t,
      };
    }
  }

  private easeOutCubic(t: number): number {
    return 1 - Math.pow(1 - t, 3);
  }

  reset() {
    this.inputHistory = [];
    this.authorityFrame = 0;
    this.localFrame = 0;
    this.correctionState = null;
    this.world = new World();
    this.world.addPlayer(this.localPlayerId);
  }
}
