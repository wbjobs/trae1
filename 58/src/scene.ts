export const WIDTH = 800;
export const HEIGHT = 600;
export const MAX_DEPTH = 4;
export const MAX_SPHERES = 8;
export const MAX_CUBES = 8;
export const MAX_PLANES = 4;
export const MAX_LIGHTS = 4;
export const MAX_TRIS = 1048576;
export const MAX_BVH_NODES = 2097152;

export type Vec3 = [number, number, number];

export interface Sphere {
  center: Vec3;
  radius: number;
  albedo: Vec3;
  emission: Vec3;
  reflectivity: number;
  refractivity: number;
  ior: number;
  pad0: number;
  pad1: number;
}

export interface Cube {
  min: Vec3;
  max: Vec3;
  albedo: Vec3;
  emission: Vec3;
  reflectivity: number;
  refractivity: number;
  ior: number;
  pad0: number;
  pad1: number;
}

export interface Plane {
  normal: Vec3;
  d: number;
  albedo: Vec3;
  emission: Vec3;
  reflectivity: number;
  refractivity: number;
  ior: number;
  pad0: number;
  pad1: number;
}

export type LightType = number;
export const LIGHT_POINT = 0;
export const LIGHT_DIR = 1;

export interface Light {
  type: number;
  pos: Vec3;
  color: Vec3;
  intensity: number;
}

export interface Scene {
  spheres: Sphere[];
  cubes: Cube[];
  planes: Plane[];
  lights: Light[];
  ambient: Vec3;
  background: Vec3;
}

export function createDefaultScene(): Scene {
  const spheres: Sphere[] = [];
  const cubes: Cube[] = [];
  const planes: Plane[] = [];
  const lights: Light[] = [];

  // ground plane (y = -1)
  planes.push({
    normal: [0, 1, 0],
    d: 1.0,
    albedo: [0.75, 0.75, 0.8],
    emission: [0, 0, 0],
    reflectivity: 0.15,
    refractivity: 0.0,
    ior: 1.5,
    pad0: 0,
    pad1: 0,
  });

  // back wall (z = -6)
  planes.push({
    normal: [0, 0, 1],
    d: 6.0,
    albedo: [0.6, 0.65, 0.7],
    emission: [0, 0, 0],
    reflectivity: 0.05,
    refractivity: 0.0,
    ior: 1.5,
    pad0: 0,
    pad1: 0,
  });

  // reflective sphere
  spheres.push({
    center: [0.0, 0.0, -1.5],
    radius: 1.0,
    albedo: [0.9, 0.9, 0.95],
    emission: [0, 0, 0],
    reflectivity: 0.9,
    refractivity: 0.0,
    ior: 1.5,
    pad0: 0,
    pad1: 0,
  });

  // glass sphere
  spheres.push({
    center: [-1.7, -0.3, 0.2],
    radius: 0.7,
    albedo: [1.0, 1.0, 1.0],
    emission: [0, 0, 0],
    reflectivity: 0.05,
    refractivity: 0.95,
    ior: 1.5,
    pad0: 0,
    pad1: 0,
  });

  // red diffuse sphere
  spheres.push({
    center: [1.8, -0.4, -0.2],
    radius: 0.6,
    albedo: [0.9, 0.15, 0.1],
    emission: [0, 0, 0],
    reflectivity: 0.2,
    refractivity: 0.0,
    ior: 1.5,
    pad0: 0,
    pad1: 0,
  });

  // green emissive sphere (small light)
  spheres.push({
    center: [0.0, 1.6, -1.5],
    radius: 0.25,
    albedo: [0.2, 0.9, 0.3],
    emission: [0.6, 2.5, 0.6],
    reflectivity: 0.0,
    refractivity: 0.0,
    ior: 1.5,
    pad0: 0,
    pad1: 0,
  });

  // yellow cube
  cubes.push({
    min: [-0.6, -1.0, -3.2],
    max: [0.6, 0.4, -2.0],
    albedo: [0.95, 0.8, 0.25],
    emission: [0, 0, 0],
    reflectivity: 0.2,
    refractivity: 0.0,
    ior: 1.5,
    pad0: 0,
    pad1: 0,
  });

  // blue small cube
  cubes.push({
    min: [1.0, -1.0, -3.5],
    max: [1.8, -0.2, -2.7],
    albedo: [0.25, 0.45, 0.95],
    emission: [0, 0, 0],
    reflectivity: 0.1,
    refractivity: 0.0,
    ior: 1.5,
    pad0: 0,
    pad1: 0,
  });

  // point light
  lights.push({
    type: LIGHT_POINT,
    pos: [2.5, 3.5, 2.0],
    color: [1.0, 0.95, 0.85],
    intensity: 18.0,
  });

  // directional light (sun)
  lights.push({
    type: LIGHT_DIR,
    pos: [0.4, -0.8, -0.3],
    color: [0.8, 0.85, 1.0],
    intensity: 1.6,
  });

  return {
    spheres,
    cubes,
    planes,
    lights,
    ambient: [0.05, 0.06, 0.08],
    background: [0.15, 0.2, 0.3],
  };
}
