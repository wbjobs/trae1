export const FixedTimestep = 1.0 / 60.0;
export const MaxVelocity = 500.0;
export const Friction = 0.92;
export const Acceleration = 1200.0;

export interface Vec2 { x: number; y: number; }

export const vec2 = (x = 0, y = 0): Vec2 => ({ x, y });
export const vAdd = (a: Vec2, b: Vec2): Vec2 => ({ x: a.x + b.x, y: a.y + b.y });
export const vSub = (a: Vec2, b: Vec2): Vec2 => ({ x: a.x - b.x, y: a.y - b.y });
export const vMul = (a: Vec2, s: number): Vec2 => ({ x: a.x * s, y: a.y * s });
export const vLen = (a: Vec2): number => Math.sqrt(a.x * a.x + a.y * a.y);
export const vNormalize = (a: Vec2): Vec2 => {
  const l = vLen(a);
  if (l < 1e-6) return vec2();
  return vMul(a, 1 / l);
};
export const vDot = (a: Vec2, b: Vec2): number => a.x * b.x + a.y * b.y;

export interface Input {
  moveX: number;
  moveY: number;
  buttons: boolean[];
  sequence: number;
}

export interface Entity {
  pos: Vec2;
  vel: Vec2;
  radius: number;
  playerId: number;
}

export interface Obstacle {
  min: Vec2;
  max: Vec2;
}

export const clamp = (v: number, lo: number, hi: number): number =>
  v < lo ? lo : v > hi ? hi : v;

export class DeterministicRNG {
  private state: bigint;
  constructor(seed: number = 12345) {
    this.state = BigInt(seed);
  }
  next(): number {
    this.state = this.state * BigInt('63641366223846793005') + BigInt('1442695040888963407');
    return Number((this.state >> 32n) & 0xffffffffn) >>> 0;
  }
  float(): number {
    return this.next() / 0xffffffff;
  }
  range(min: number, max: number): number {
    return min + this.float() * (max - min);
  }
}

const collides = (pos: Vec2, r: number, obs: Obstacle): boolean => {
  const closest = vec2(
    clamp(pos.x, obs.min.x, obs.max.x),
    clamp(pos.y, obs.min.y, obs.max.y)
  );
  return vLen(vSub(pos, closest)) < r;
};

const resolveCollision = (oldPos: Vec2, newPos: Vec2, r: number, obs: Obstacle): Vec2 => {
  const cx = clamp(newPos.x, obs.min.x, obs.max.x);
  const cy = clamp(newPos.y, obs.min.y, obs.max.y);
  let dx = newPos.x - cx;
  let dy = newPos.y - cy;
  if (dx === 0 && dy === 0) {
    if (oldPos.x < obs.min.x) return vec2(-1, 0);
    if (oldPos.x > obs.max.x) return vec2(1, 0);
    if (oldPos.y < obs.min.y) return vec2(0, -1);
    return vec2(0, 1);
  }
  const dist = Math.sqrt(dx * dx + dy * dy);
  return vNormalize(vec2(dx / dist, dy / dist));
};

export const defaultObstacles: Obstacle[] = [
  { min: vec2(-400, -300), max: vec2(-350, 300) },
  { min: vec2(350, -300), max: vec2(400, 300) },
  { min: vec2(-400, -300), max: vec2(400, -250) },
  { min: vec2(-400, 250), max: vec2(400, 300) },
  { min: vec2(-100, -100), max: vec2(100, 100) },
];

export class World {
  entities: Entity[] = [];
  obstacles: Obstacle[] = defaultObstacles;
  time = 0;
  frame = 0;
  rng: DeterministicRNG;

  constructor(seed = 12345) {
    this.rng = new DeterministicRNG(seed);
  }

  addPlayer(id: number) {
    for (const e of this.entities) {
      if (e.playerId === id) return;
    }
    this.entities.push({
      pos: vec2(id * 50 - 75, 0),
      vel: vec2(),
      radius: 20,
      playerId: id,
    });
  }

  removePlayer(id: number) {
    const idx = this.entities.findIndex(e => e.playerId === id);
    if (idx >= 0) this.entities.splice(idx, 1);
  }

  step(inputs: Map<number, Input>) {
    this.frame++;
    this.time += FixedTimestep;

    for (const e of this.entities) {
      const input = inputs.get(e.playerId);
      if (!input) {
        e.vel = vMul(e.vel, Friction);
      } else {
        let move = vec2(input.moveX, input.moveY);
        if (vLen(move) > 1) move = vNormalize(move);
        const accel = vMul(move, Acceleration * FixedTimestep);
        e.vel = vAdd(e.vel, accel);
        e.vel = vMul(e.vel, Friction);
        if (vLen(e.vel) > MaxVelocity) {
          e.vel = vMul(vNormalize(e.vel), MaxVelocity);
        }
      }

      const delta = vMul(e.vel, FixedTimestep);
      let newPos = vAdd(e.pos, delta);

      for (const obs of this.obstacles) {
        if (collides(newPos, e.radius, obs)) {
          const normal = resolveCollision(e.pos, newPos, e.radius, obs);
          newPos = vAdd(e.pos, vSub(delta, vMul(normal, vDot(delta, normal) * 1.01)));
          e.vel = vSub(e.vel, vMul(normal, vDot(e.vel, normal) * 1.01));
          break;
        }
      }

      e.pos = newPos;
    }
  }

  getPlayer(id: number): Entity | null {
    return this.entities.find(e => e.playerId === id) || null;
  }

  snapshot(): Entity[] {
    return this.entities.map(e => ({ ...e, pos: { ...e.pos }, vel: { ...e.vel } }));
  }

  clone(): World {
    const w = new World();
    w.entities = this.snapshot();
    w.obstacles = [...this.obstacles];
    w.time = this.time;
    w.frame = this.frame;
    return w;
  }
}
