import * as m from './math.js';
import type { Vec3 } from './math.js';

export interface CameraState {
  target: Vec3;
  distance: number;
  yaw: number;
  pitch: number;
  fov: number;
}

export interface CameraUniforms {
  origin: Vec3;
  right: Vec3;
  up: Vec3;
  forward: Vec3;
  fovScale: number;
  aspect: number;
}

export function createDefaultCamera(): CameraState {
  return {
    target: { x: 0, y: 0, z: -1.5 },
    distance: 6.5,
    yaw: 0,
    pitch: 0.25,
    fov: 50,
  };
}

export function computeCameraUniforms(state: CameraState, aspect: number): CameraUniforms {
  const cp = Math.cos(state.pitch);
  const sp = Math.sin(state.pitch);
  const cy = Math.cos(state.yaw);
  const sy = Math.sin(state.yaw);
  // forward from camera toward target; negate so origin = target - forward*distance
  const forward: Vec3 = {
    x: cp * sy,
    y: -sp,
    z: cp * cy,
  };
  const worldUp: Vec3 = { x: 0, y: 1, z: 0 };
  const right = m.normalize(m.cross(forward, worldUp));
  const up = m.normalize(m.cross(right, forward));
  const origin = m.sub(state.target, m.scale(forward, state.distance));
  const fovScale = Math.tan((state.fov * Math.PI) / 360);
  return {
    origin,
    right,
    up,
    forward,
    fovScale,
    aspect,
  };
}
