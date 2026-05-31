import { SHADER_CODE } from './shader.js';
import {
  WIDTH,
  HEIGHT,
  MAX_SPHERES,
  MAX_CUBES,
  MAX_PLANES,
  MAX_LIGHTS,
  type Scene,
} from './scene.js';
import type { CameraUniforms } from './camera.js';
import type { TriData } from './obj.js';
import type { BVHResult } from './bvh.js';

// Struct layout (matches shader.ts) — each row is a vec4 (16 bytes):
//
// Sphere (20 f32 = 80 bytes):
//   row0: cx cy cz r
//   row1: ax ay az 0
//   row2: ex ey ez 0
//   row3: rr rt ior 0
//   row4: 0 0 0 0
//
// Cube (20 f32 = 80 bytes):
//   row0: min_x min_y min_z 0
//   row1: max_x max_y max_z 0
//   row2: ax ay az 0
//   row3: ex ey ez 0
//   row4: rr rt ior 0
//
// Plane (20 f32 = 80 bytes):
//   row0: nx ny nz d
//   row1: ax ay az 0
//   row2: ex ey ez 0
//   row3: rr rt ior 0
//   row4: 0 0 0 0
//
// Light (12 f32 = 48 bytes):
//   row0: type_f 0 0 0
//   row1: px py pz 0
//   row2: cr cg cb intensity
//
// Camera (20 f32 = 80 bytes):
//   row0: ox oy oz 0
//   row1: rx ry rz 0
//   row2: ux uy uz 0
//   row3: fx fy fz 0
//   row4: fovScale aspect 0 0
//
// SceneUniforms (16 u32 = 64 bytes):
//   row0: ambient.r, ambient.g, ambient.b, 0
//   row1: bg.r, bg.g, bg.b, 0
//   row2: numS, numC, numP, numL
//   row3: frameCount, numTris, debugBVH, 0
//
// Triangle (24 f32 = 96 bytes):
//   row0: v0.x, v0.y, v0.z, nx
//   row1: v1.x, v1.y, v1.z, ny
//   row2: v2.x, v2.y, v2.z, nz
//   row3: albedo.r, albedo.g, albedo.b, emission.r
//   row4: emission.g, emission.b, reflectivity, refractivity
//   row5: ior, 0, 0, 0
//
// BVHNode (12 u32/f32 = 48 bytes):
//   row0: bboxMin.x, bboxMin.y, bboxMin.z, 0
//   row1: bboxMax.x, bboxMax.y, bboxMax.z, 0
//   row2: leftFirst, triCount, 0, 0

export const SPHERE_STRIDE = 20;
export const CUBE_STRIDE = 20;
export const PLANE_STRIDE = 20;
export const LIGHT_STRIDE = 12;
export const TRI_STRIDE = 24; // 6 x vec4
export const BVH_STRIDE = 12;  // 3 x vec4

export interface Renderer {
  device: GPUDevice;
  bindGroup: GPUBindGroup;
  pipeline: GPUComputePipeline;
  outputBuf: GPUBuffer;
  frameCount: number;
  resetAccum(): void;
  updateScene(scene: Scene): void;
  updateCamera(cam: CameraUniforms): void;
  updateMesh(tris: TriData[], bvh: BVHResult, material: {
    albedo: [number, number, number];
    emission: [number, number, number];
    reflectivity: number;
    refractivity: number;
    ior: number;
  }): void;
  setDebugBVH(on: boolean): void;
  dispatch(enc: GPUCommandEncoder): void;
  destroy(): void;
}

function buf(device: GPUDevice, size: number, usage: number): GPUBuffer {
  return device.createBuffer({ size, usage });
}

export async function initRenderer(device: GPUDevice): Promise<Renderer> {
  const sphereBytes = MAX_SPHERES * SPHERE_STRIDE * 4;
  const cubeBytes = MAX_CUBES * CUBE_STRIDE * 4;
  const planeBytes = MAX_PLANES * PLANE_STRIDE * 4;
  const lightBytes = MAX_LIGHTS * LIGHT_STRIDE * 4;
  const cameraBytes = 20 * 4;
  const sceneBytes = 16 * 4;
  const pixels = WIDTH * HEIGHT;
  const pixelBytes = pixels * 16;

  // Triangle + BVH buffers sized for max capacity
  const triBytes = MAX_SPHERES * 0; // placeholder, actual size in updateMesh
  // We allocate max-sized buffers for triangles and BVH nodes
  const maxTriBytes = 1048576 * TRI_STRIDE * 4; // 1M * 96 bytes
  const maxBVHBytes = 2097152 * BVH_STRIDE * 4; // 2M * 48 bytes

  const sphereBuf = buf(device, sphereBytes, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
  const cubeBuf = buf(device, cubeBytes, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
  const planeBuf = buf(device, planeBytes, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
  const lightBuf = buf(device, lightBytes, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
  const cameraBuf = buf(device, cameraBytes, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST);
  const sceneBuf = buf(device, sceneBytes, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST);
  const accumBuf = buf(device, pixelBytes, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
  const outputBuf = buf(
    device,
    pixelBytes,
    GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC
  );
  const triBuf = buf(device, maxTriBytes, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
  const bvhBuf = buf(device, maxBVHBytes, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);

  let numTris = 0;
  let debugBVH = 0;

  const module = device.createShaderModule({ code: SHADER_CODE });

  const bindGroupLayout = device.createBindGroupLayout({
    entries: [
      { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
      { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
      { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
      { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
      { binding: 4, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } },
      { binding: 5, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } },
      { binding: 6, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
      { binding: 7, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
      { binding: 8, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
      { binding: 9, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
    ],
  });

  const pipeline = device.createComputePipeline({
    layout: device.createPipelineLayout({ bindGroupLayouts: [bindGroupLayout] }),
    compute: { module, entryPoint: 'main' },
  });

  const bindGroup = device.createBindGroup({
    layout: bindGroupLayout,
    entries: [
      { binding: 0, resource: { buffer: sphereBuf } },
      { binding: 1, resource: { buffer: cubeBuf } },
      { binding: 2, resource: { buffer: planeBuf } },
      { binding: 3, resource: { buffer: lightBuf } },
      { binding: 4, resource: { buffer: cameraBuf } },
      { binding: 5, resource: { buffer: sceneBuf } },
      { binding: 6, resource: { buffer: accumBuf } },
      { binding: 7, resource: { buffer: outputBuf } },
      { binding: 8, resource: { buffer: triBuf } },
      { binding: 9, resource: { buffer: bvhBuf } },
    ],
  });

  return {
    device,
    bindGroup,
    pipeline,
    outputBuf,
    frameCount: 0,
    resetAccum() {
      const zero = new Float32Array(WIDTH * HEIGHT * 4);
      device.queue.writeBuffer(accumBuf, 0, zero);
      this.frameCount = 0;
    },
    updateScene(scene: Scene) {
      {
        const data = new Float32Array(MAX_SPHERES * SPHERE_STRIDE);
        for (let i = 0; i < scene.spheres.length; i++) {
          const s = scene.spheres[i];
          const o = i * SPHERE_STRIDE;
          data[o + 0] = s.center[0];
          data[o + 1] = s.center[1];
          data[o + 2] = s.center[2];
          data[o + 3] = s.radius;
          data[o + 4] = s.albedo[0];
          data[o + 5] = s.albedo[1];
          data[o + 6] = s.albedo[2];
          data[o + 8] = s.emission[0];
          data[o + 9] = s.emission[1];
          data[o + 10] = s.emission[2];
          data[o + 12] = s.reflectivity;
          data[o + 13] = s.refractivity;
          data[o + 14] = s.ior;
        }
        device.queue.writeBuffer(sphereBuf, 0, data);
      }
      {
        const data = new Float32Array(MAX_CUBES * CUBE_STRIDE);
        for (let i = 0; i < scene.cubes.length; i++) {
          const c = scene.cubes[i];
          const o = i * CUBE_STRIDE;
          data[o + 0] = c.min[0];
          data[o + 1] = c.min[1];
          data[o + 2] = c.min[2];
          data[o + 4] = c.max[0];
          data[o + 5] = c.max[1];
          data[o + 6] = c.max[2];
          data[o + 8] = c.albedo[0];
          data[o + 9] = c.albedo[1];
          data[o + 10] = c.albedo[2];
          data[o + 12] = c.emission[0];
          data[o + 13] = c.emission[1];
          data[o + 14] = c.emission[2];
          data[o + 16] = c.reflectivity;
          data[o + 17] = c.refractivity;
          data[o + 18] = c.ior;
        }
        device.queue.writeBuffer(cubeBuf, 0, data);
      }
      {
        const data = new Float32Array(MAX_PLANES * PLANE_STRIDE);
        for (let i = 0; i < scene.planes.length; i++) {
          const p = scene.planes[i];
          const o = i * PLANE_STRIDE;
          data[o + 0] = p.normal[0];
          data[o + 1] = p.normal[1];
          data[o + 2] = p.normal[2];
          data[o + 3] = p.d;
          data[o + 4] = p.albedo[0];
          data[o + 5] = p.albedo[1];
          data[o + 6] = p.albedo[2];
          data[o + 8] = p.emission[0];
          data[o + 9] = p.emission[1];
          data[o + 10] = p.emission[2];
          data[o + 12] = p.reflectivity;
          data[o + 13] = p.refractivity;
          data[o + 14] = p.ior;
        }
        device.queue.writeBuffer(planeBuf, 0, data);
      }
      {
        const data = new Float32Array(MAX_LIGHTS * LIGHT_STRIDE);
        for (let i = 0; i < scene.lights.length; i++) {
          const l = scene.lights[i];
          const o = i * LIGHT_STRIDE;
          data[o + 0] = l.type;
          data[o + 4] = l.pos[0];
          data[o + 5] = l.pos[1];
          data[o + 6] = l.pos[2];
          data[o + 8] = l.color[0];
          data[o + 9] = l.color[1];
          data[o + 10] = l.color[2];
          data[o + 11] = l.intensity;
        }
        device.queue.writeBuffer(lightBuf, 0, data);
      }
      // SceneUniforms
      {
        const fBuf = new Float32Array(8);
        fBuf[0] = scene.ambient[0];
        fBuf[1] = scene.ambient[1];
        fBuf[2] = scene.ambient[2];
        fBuf[4] = scene.background[0];
        fBuf[5] = scene.background[1];
        fBuf[6] = scene.background[2];
        const uBuf = new Uint32Array(fBuf.buffer);
        const sceneData = new Uint32Array(16);
        sceneData[0] = uBuf[0];
        sceneData[1] = uBuf[1];
        sceneData[2] = uBuf[2];
        sceneData[4] = uBuf[4];
        sceneData[5] = uBuf[5];
        sceneData[6] = uBuf[6];
        sceneData[8] = scene.spheres.length;
        sceneData[9] = scene.cubes.length;
        sceneData[10] = scene.planes.length;
        sceneData[11] = scene.lights.length;
        sceneData[12] = 0; // frameCount written at dispatch
        sceneData[13] = numTris;
        sceneData[14] = debugBVH;
        device.queue.writeBuffer(sceneBuf, 0, sceneData);
      }
    },
    updateCamera(cam: CameraUniforms) {
      const data = new Float32Array(20);
      data[0] = cam.origin.x;
      data[1] = cam.origin.y;
      data[2] = cam.origin.z;
      data[4] = cam.right.x;
      data[5] = cam.right.y;
      data[6] = cam.right.z;
      data[8] = cam.up.x;
      data[9] = cam.up.y;
      data[10] = cam.up.z;
      data[12] = cam.forward.x;
      data[13] = cam.forward.y;
      data[14] = cam.forward.z;
      data[16] = cam.fovScale;
      data[17] = cam.aspect;
      device.queue.writeBuffer(cameraBuf, 0, data);
    },
    updateMesh(tris, bvh, material) {
      numTris = tris.length;

      // Upload triangles (reordered by BVH permutation)
      const triData = new Float32Array(numTris * TRI_STRIDE);
      const perm = bvh.triIndices;
      for (let i = 0; i < numTris; i++) {
        const srcIdx = perm[i];
        const t = tris[srcIdx];
        const o = i * TRI_STRIDE;
        // v0 + nx
        triData[o + 0] = t.v0.x;
        triData[o + 1] = t.v0.y;
        triData[o + 2] = t.v0.z;
        triData[o + 3] = t.n.x;
        // v1 + ny
        triData[o + 4] = t.v1.x;
        triData[o + 5] = t.v1.y;
        triData[o + 6] = t.v1.z;
        triData[o + 7] = t.n.y;
        // v2 + nz
        triData[o + 8] = t.v2.x;
        triData[o + 9] = t.v2.y;
        triData[o + 10] = t.v2.z;
        triData[o + 11] = t.n.z;
        // albedo.rgb + emission.r
        triData[o + 12] = material.albedo[0];
        triData[o + 13] = material.albedo[1];
        triData[o + 14] = material.albedo[2];
        triData[o + 15] = material.emission[0];
        // emission.gb + refl + refr
        triData[o + 16] = material.emission[1];
        triData[o + 17] = material.emission[2];
        triData[o + 18] = material.reflectivity;
        triData[o + 19] = material.refractivity;
        // ior + pad
        triData[o + 20] = material.ior;
      }
      device.queue.writeBuffer(triBuf, 0, triData);

      // Upload BVH nodes
      device.queue.writeBuffer(bvhBuf, 0, bvh.nodes.buffer, bvh.nodes.byteOffset, bvh.nodes.byteLength);

      // Update scene uniform
      const sceneData = new Uint32Array(16);
      sceneData[13] = numTris;
      sceneData[14] = debugBVH;
      device.queue.writeBuffer(sceneBuf, 13 * 4, sceneData.subarray(13, 15));
    },
    setDebugBVH(on: boolean) {
      debugBVH = on ? 1 : 0;
      const v = new Uint32Array(1);
      v[0] = debugBVH;
      device.queue.writeBuffer(sceneBuf, 14 * 4, v);
    },
    dispatch(enc: GPUCommandEncoder) {
      const fa = new Uint32Array(1);
      fa[0] = this.frameCount;
      device.queue.writeBuffer(sceneBuf, 12 * 4, fa);
      this.frameCount++;

      const pass = enc.beginComputePass();
      pass.setPipeline(pipeline);
      pass.setBindGroup(0, bindGroup);
      const wgX = Math.ceil(WIDTH / 8);
      const wgY = Math.ceil(HEIGHT / 8);
      pass.dispatchWorkgroups(wgX, wgY, 1);
      pass.end();
    },
    destroy() {
      sphereBuf.destroy();
      cubeBuf.destroy();
      planeBuf.destroy();
      lightBuf.destroy();
      cameraBuf.destroy();
      sceneBuf.destroy();
      accumBuf.destroy();
      outputBuf.destroy();
      triBuf.destroy();
      bvhBuf.destroy();
    },
  } as Renderer;
}
