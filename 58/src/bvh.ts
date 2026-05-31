import type { Vec3 } from './math.js';
import type { TriData } from './obj.js';

export interface BuildStats {
  nodeCount: number;
  leafCount: number;
  maxDepth: number;
  buildTimeMs: number;
}

export interface BVHResult {
  // Reordered triangle indices (maps reordered tris back to original)
  triIndices: Int32Array;
  // Node data: 12 u32/f32 per node. Stored as Float32Array but with u32 fields
  // written via a Uint32Array view. Layout per node:
  //   [0]  bboxMinX     f32
  //   [1]  bboxMinY     f32
  //   [2]  bboxMinZ     f32
  //   [3]  pad          f32
  //   [4]  bboxMaxX     f32
  //   [5]  bboxMaxY     f32
  //   [6]  bboxMaxZ     f32
  //   [7]  pad          f32
  //   [8]  leftFirst    u32 (child node index, or first tri index if leaf)
  //   [9]  triCount     u32 (0 = interior, >0 = leaf)
  //   [10] pad          u32
  //   [11] pad          u32
  nodes: Float32Array;
  nodeCount: number;
  stats: BuildStats;
}

const BINS = 12;
const LEAF_SIZE = 2; // max tris per leaf
const MAX_DEPTH = 24;

interface Bin {
  count: number;
  minX: number;
  minY: number;
  minZ: number;
  maxX: number;
  maxY: number;
  maxZ: number;
}

function emptyBin(): Bin {
  return {
    count: 0,
    minX: Infinity, minY: Infinity, minZ: Infinity,
    maxX: -Infinity, maxY: -Infinity, maxZ: -Infinity,
  };
}

function binAdd(b: Bin, x: number, y: number, z: number) {
  b.count++;
  if (x < b.minX) b.minX = x;
  if (y < b.minY) b.minY = y;
  if (z < b.minZ) b.minZ = z;
  if (x > b.maxX) b.maxX = x;
  if (y > b.maxY) b.maxY = y;
  if (z > b.maxZ) b.maxZ = z;
}

function binMerge(a: Bin, b: Bin): Bin {
  return {
    count: a.count + b.count,
    minX: Math.min(a.minX, b.minX),
    minY: Math.min(a.minY, b.minY),
    minZ: Math.min(a.minZ, b.minZ),
    maxX: Math.max(a.maxX, b.maxX),
    maxY: Math.max(a.maxY, b.maxY),
    maxZ: Math.max(a.maxZ, b.maxZ),
  };
}

function binArea(b: Bin): number {
  const dx = b.maxX - b.minX;
  const dy = b.maxY - b.minY;
  const dz = b.maxZ - b.minZ;
  if (dx <= 0 || dy <= 0 || dz <= 0) return 0;
  return dx * dy + dx * dz + dy * dz;
}

export function buildBVH(tris: TriData[]): BVHResult {
  const t0 = performance.now();

  const n = tris.length;
  // centroid x, y, z
  const cx = new Float32Array(n);
  const cy = new Float32Array(n);
  const cz = new Float32Array(n);
  // triangle bbox
  const tminX = new Float32Array(n);
  const tminY = new Float32Array(n);
  const tminZ = new Float32Array(n);
  const tmaxX = new Float32Array(n);
  const tmaxY = new Float32Array(n);
  const tmaxZ = new Float32Array(n);

  for (let i = 0; i < n; i++) {
    const t = tris[i];
    const v0 = t.v0, v1 = t.v1, v2 = t.v2;
    const mnx = Math.min(v0.x, v1.x, v2.x);
    const mny = Math.min(v0.y, v1.y, v2.y);
    const mnz = Math.min(v0.z, v1.z, v2.z);
    const mxx = Math.max(v0.x, v1.x, v2.x);
    const mxy = Math.max(v0.y, v1.y, v2.y);
    const mxz = Math.max(v0.z, v1.z, v2.z);
    tminX[i] = mnx; tminY[i] = mny; tminZ[i] = mnz;
    tmaxX[i] = mxx; tmaxY[i] = mxy; tmaxZ[i] = mxz;
    cx[i] = (mnx + mxx) * 0.5;
    cy[i] = (mny + mxy) * 0.5;
    cz[i] = (mnz + mxz) * 0.5;
  }

  // permutation array
  const perm = new Int32Array(n);
  for (let i = 0; i < n; i++) perm[i] = i;

  // node storage — worst case ~2N
  const maxNodes = Math.min(n * 2, 4 * 1024 * 1024);
  const nodeBuf = new ArrayBuffer(maxNodes * 48);
  const nodesF = new Float32Array(nodeBuf);
  const nodesU = new Uint32Array(nodeBuf);
  let nodeCount = 0;

  // SAH cost constants
  const C_TRAV = 0.5;
  const C_ISCT = 1.0;

  function allocNode(): number {
    const idx = nodeCount * 12;
    nodeCount++;
    // zero out
    for (let i = 0; i < 12; i++) nodesF[idx + i] = 0;
    return idx;
  }

  function setNodeBBox(idx: number, mnX: number, mnY: number, mnZ: number, mxX: number, mxY: number, mxZ: number) {
    nodesF[idx + 0] = mnX;
    nodesF[idx + 1] = mnY;
    nodesF[idx + 2] = mnZ;
    nodesF[idx + 4] = mxX;
    nodesF[idx + 5] = mxY;
    nodesF[idx + 6] = mxZ;
  }

  function setNodeLeftFirst(idx: number, val: number) {
    nodesU[idx + 8] = val;
  }

  function setNodeTriCount(idx: number, val: number) {
    nodesU[idx + 9] = val;
  }

  // iterative build using explicit stack
  // stack entries: [permStart, permEnd, nodeSlot, depth]
  const stackSize = 256;
  const stackA = new Int32Array(stackSize * 4);
  let stackTop = 0;

  const root = allocNode();
  // push root with full range
  stackA[0] = 0;
  stackA[1] = n;
  stackA[2] = root;
  stackA[3] = 0;
  stackTop = 4;

  let leafCount = 0;
  let maxDepthObserved = 0;

  // temp bins
  const bins: Bin[] = [];
  for (let i = 0; i < BINS; i++) bins.push(emptyBin());

  while (stackTop > 0) {
    stackTop -= 4;
    const pStart = stackA[stackTop];
    const pEnd = stackA[stackTop + 1];
    const nodeIdx = stackA[stackTop + 2];
    const depth = stackA[stackTop + 3];
    if (depth > maxDepthObserved) maxDepthObserved = depth;

    const count = pEnd - pStart;

    // compute overall bbox
    let nMinX = Infinity, nMinY = Infinity, nMinZ = Infinity;
    let nMaxX = -Infinity, nMaxY = -Infinity, nMaxZ = -Infinity;
    for (let i = pStart; i < pEnd; i++) {
      const ti = perm[i];
      if (tminX[ti] < nMinX) nMinX = tminX[ti];
      if (tminY[ti] < nMinY) nMinY = tminY[ti];
      if (tminZ[ti] < nMinZ) nMinZ = tminZ[ti];
      if (tmaxX[ti] > nMaxX) nMaxX = tmaxX[ti];
      if (tmaxY[ti] > nMaxY) nMaxY = tmaxY[ti];
      if (tmaxZ[ti] > nMaxZ) nMaxZ = tmaxZ[ti];
    }
    setNodeBBox(nodeIdx, nMinX, nMinY, nMinZ, nMaxX, nMaxY, nMaxZ);

    // leaf?
    if (count <= LEAF_SIZE || depth >= MAX_DEPTH) {
      setNodeLeftFirst(nodeIdx, pStart);
      setNodeTriCount(nodeIdx, count);
      leafCount++;
      continue;
    }

    // find best axis + split
    let bestAxis = 0;
    let bestSplit = 0;
    let bestCost = Infinity;

    for (let axis = 0; axis < 3; axis++) {
      const ca = axis === 0 ? cx : axis === 1 ? cy : cz;
      let mn = Infinity, mx = -Infinity;
      for (let i = pStart; i < pEnd; i++) {
        const c = ca[perm[i]];
        if (c < mn) mn = c;
        if (c > mx) mx = c;
      }
      if (mx - mn < 1e-6) continue;

      // reset bins
      for (let b = 0; b < BINS; b++) {
        bins[b].count = 0;
        bins[b].minX = Infinity; bins[b].minY = Infinity; bins[b].minZ = Infinity;
        bins[b].maxX = -Infinity; bins[b].maxY = -Infinity; bins[b].maxZ = -Infinity;
      }

      const range = mx - mn;
      for (let i = pStart; i < pEnd; i++) {
        const ti = perm[i];
        let bi = Math.floor(((ca[ti] - mn) / range) * BINS);
        if (bi >= BINS) bi = BINS - 1;
        if (bi < 0) bi = 0;
        bins[bi].count++;
        if (tminX[ti] < bins[bi].minX) bins[bi].minX = tminX[ti];
        if (tminY[ti] < bins[bi].minY) bins[bi].minY = tminY[ti];
        if (tminZ[ti] < bins[bi].minZ) bins[bi].minZ = tminZ[ti];
        if (tmaxX[ti] > bins[bi].maxX) bins[bi].maxX = tmaxX[ti];
        if (tmaxY[ti] > bins[bi].maxY) bins[bi].maxY = tmaxY[ti];
        if (tmaxZ[ti] > bins[bi].maxZ) bins[bi].maxZ = tmaxZ[ti];
      }

      // right-to-left prefix sums
      const rightCounts = new Int32Array(BINS + 1);
      const rightBoxes: Bin[] = [];
      for (let i = 0; i <= BINS; i++) rightBoxes.push(emptyBin());
      rightBoxes[BINS].count = 0;
      rightCounts[BINS] = 0;
      for (let b = BINS - 1; b >= 0; b--) {
        rightBoxes[b] = binMerge(rightBoxes[b + 1], bins[b]);
        rightCounts[b] = rightCounts[b + 1] + bins[b].count;
      }

      // left-to-right sweep
      let leftBox = emptyBin();
      let leftCount = 0;
      for (let b = 0; b < BINS - 1; b++) {
        leftBox = binMerge(leftBox, bins[b]);
        leftCount += bins[b].count;
        if (leftCount === 0) continue;
        const rightCount = rightCounts[b + 1];
        if (rightCount === 0) continue;
        const cost = C_TRAV +
          (C_ISCT * binArea(leftBox) * leftCount +
            C_ISCT * binArea(rightBoxes[b + 1]) * rightCount) /
            binArea({
              count: 1,
              minX: nMinX, minY: nMinY, minZ: nMinZ,
              maxX: nMaxX, maxY: nMaxY, maxZ: nMaxZ,
            });
        if (cost < bestCost) {
          bestCost = cost;
          bestAxis = axis;
          bestSplit = b + 1;
        }
      }
    }

    const leafCost = C_ISCT * count;
    if (bestCost >= leafCost) {
      setNodeLeftFirst(nodeIdx, pStart);
      setNodeTriCount(nodeIdx, count);
      leafCount++;
      continue;
    }

    // partition perm array
    const ca = bestAxis === 0 ? cx : bestAxis === 1 ? cy : cz;
    let bboxMin = Infinity, bboxMax = -Infinity;
    for (let i = pStart; i < pEnd; i++) {
      const c = ca[perm[i]];
      if (c < bboxMin) bboxMin = c;
      if (c > bboxMax) bboxMax = c;
    }
    const range = bboxMax - bboxMin;
    const threshold = bboxMin + (bestSplit / BINS) * range;

    // two-pointer partition
    let lo = pStart;
    let hi = pEnd - 1;
    while (lo <= hi) {
      if (ca[perm[lo]] <= threshold) {
        lo++;
      } else if (ca[perm[hi]] > threshold) {
        hi--;
      } else {
        const tmp = perm[lo];
        perm[lo] = perm[hi];
        perm[hi] = tmp;
        lo++;
        hi--;
      }
    }
    const splitIdx = lo; // perm[pStart..splitIdx) go left, perm[splitIdx..pEnd) go right

    if (splitIdx === pStart || splitIdx === pEnd) {
      // degenerate split: force a leaf
      setNodeLeftFirst(nodeIdx, pStart);
      setNodeTriCount(nodeIdx, count);
      leafCount++;
      continue;
    }

    // interior node
    setNodeTriCount(nodeIdx, 0);

    // alloc children
    const leftIdx = allocNode();
    const rightIdx = allocNode();
    setNodeLeftFirst(nodeIdx, leftIdx);

    // push right first (so left is processed first)
    if (stackTop + 8 > stackSize * 4) {
      // should not happen with MAX_DEPTH=24 and 256 slots (256*4 = 1024)
      setNodeLeftFirst(nodeIdx, pStart);
      setNodeTriCount(nodeIdx, count);
      leafCount++;
      continue;
    }
    stackA[stackTop] = splitIdx;
    stackA[stackTop + 1] = pEnd;
    stackA[stackTop + 2] = rightIdx;
    stackA[stackTop + 3] = depth + 1;
    stackTop += 4;
    stackA[stackTop] = pStart;
    stackA[stackTop + 1] = splitIdx;
    stackA[stackTop + 2] = leftIdx;
    stackA[stackTop + 3] = depth + 1;
    stackTop += 4;
  }

  const t1 = performance.now();

  return {
    triIndices: perm,
    nodes: nodesF.slice(0, nodeCount * 12),
    nodeCount,
    stats: {
      nodeCount,
      leafCount,
      maxDepth: maxDepthObserved,
      buildTimeMs: t1 - t0,
    },
  };
}
