import type { Vec3 } from './math.js';

export interface ParsedOBJ {
  triangles: TriData[];
  bounds: { min: Vec3; max: Vec3 };
}

export interface TriData {
  v0: Vec3;
  v1: Vec3;
  v2: Vec3;
  n: Vec3; // geometric normal (area-weighted average of face normals)
}

export function parseOBJ(text: string): ParsedOBJ {
  const lines = text.split('\n');
  const verts: Vec3[] = [];
  const faces: number[][] = [];

  for (const line of lines) {
    const trimmed = line.trim();
    if (trimmed.startsWith('v ')) {
      const parts = trimmed.split(/\s+/);
      verts.push({
        x: parseFloat(parts[1]),
        y: parseFloat(parts[2]),
        z: parseFloat(parts[3]),
      });
    } else if (trimmed.startsWith('f ')) {
      const parts = trimmed.split(/\s+/).slice(1);
      const indices: number[] = [];
      for (const p of parts) {
        // supports "i", "i/j", "i/j/k", "i//k"
        const idx = parseInt(p.split('/')[0], 10);
        indices.push(idx > 0 ? idx - 1 : verts.length + idx);
      }
      // triangulate fan
      for (let i = 1; i < indices.length - 1; i++) {
        faces.push([indices[0], indices[i], indices[i + 1]]);
      }
    }
  }

  const tris: TriData[] = [];
  for (const f of faces) {
    const v0 = verts[f[0]];
    const v1 = verts[f[1]];
    const v2 = verts[f[2]];
    const e1 = { x: v1.x - v0.x, y: v1.y - v0.y, z: v1.z - v0.z };
    const e2 = { x: v2.x - v0.x, y: v2.y - v0.y, z: v2.z - v0.z };
    const n = {
      x: e1.y * e2.z - e1.z * e2.y,
      y: e1.z * e2.x - e1.x * e2.z,
      z: e1.x * e2.y - e1.y * e2.x,
    };
    tris.push({ v0, v1, v2, n });
  }

  // compute bounds
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  for (const t of tris) {
    for (const v of [t.v0, t.v1, t.v2]) {
      if (v.x < minX) minX = v.x;
      if (v.y < minY) minY = v.y;
      if (v.z < minZ) minZ = v.z;
      if (v.x > maxX) maxX = v.x;
      if (v.y > maxY) maxY = v.y;
      if (v.z > maxZ) maxZ = v.z;
    }
  }

  return {
    triangles: tris,
    bounds: {
      min: { x: minX, y: minY, z: minZ },
      max: { x: maxX, y: maxY, z: maxZ },
    },
  };
}

// Load a built-in sample OBJ (a simple model) when no file is provided.
// Returns a teapot-like model or a simple grid of boxes.
export function generateSampleModel(count = 1000): ParsedOBJ {
  const tris: TriData[] = [];
  const n = Math.ceil(Math.cbrt(count));
  const spacing = 2.0 / n;

  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      for (let k = 0; k < n; k++) {
        if (tris.length >= count) break;
        const cx = -1.0 + i * spacing + spacing / 2;
        const cy = -1.0 + j * spacing + spacing / 2;
        const cz = -1.0 + k * spacing + spacing / 2;
        const s = spacing * 0.4;
        const verts = [
          { x: cx - s, y: cy - s, z: cz - s },
          { x: cx + s, y: cy - s, z: cz - s },
          { x: cx + s, y: cy + s, z: cz - s },
          { x: cx - s, y: cy + s, z: cz - s },
          { x: cx - s, y: cy - s, z: cz + s },
          { x: cx + s, y: cy - s, z: cz + s },
          { x: cx + s, y: cy + s, z: cz + s },
          { x: cx - s, y: cy + s, z: cz + s },
        ];
        // 12 triangles per box
        const boxFaces = [
          [0, 1, 2], [0, 2, 3],   // bottom
          [4, 6, 5], [4, 7, 6],   // top
          [0, 4, 5], [0, 5, 1],   // front
          [1, 5, 6], [1, 6, 2],   // right
          [2, 6, 7], [2, 7, 3],   // back
          [3, 7, 4], [3, 4, 0],   // left
        ];
        for (const f of boxFaces) {
          const v0 = verts[f[0]], v1 = verts[f[1]], v2 = verts[f[2]];
          const e1 = { x: v1.x - v0.x, y: v1.y - v0.y, z: v1.z - v0.z };
          const e2 = { x: v2.x - v0.x, y: v2.y - v0.y, z: v2.z - v0.z };
          const nx = e1.y * e2.z - e1.z * e2.y;
          const ny = e1.z * e2.x - e1.x * e2.z;
          const nz = e1.x * e2.y - e1.y * e2.x;
          const len = Math.sqrt(nx * nx + ny * ny + nz * nz);
          tris.push({
            v0, v1, v2,
            n: { x: nx / len, y: ny / len, z: nz / len },
          });
        }
      }
      if (tris.length >= count) break;
    }
    if (tris.length >= count) break;
  }

  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  for (const t of tris) {
    for (const v of [t.v0, t.v1, t.v2]) {
      if (v.x < minX) minX = v.x;
      if (v.y < minY) minY = v.y;
      if (v.z < minZ) minZ = v.z;
      if (v.x > maxX) maxX = v.x;
      if (v.y > maxY) maxY = v.y;
      if (v.z > maxZ) maxZ = v.z;
    }
  }
  return {
    triangles: tris,
    bounds: { min: { x: minX, y: minY, z: minZ }, max: { x: maxX, y: maxY, z: maxZ } },
  };
}
