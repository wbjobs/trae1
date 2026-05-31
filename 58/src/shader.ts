// WebGPU Whitted-style ray tracing compute shader with BVH acceleration.
// All structs are explicitly padded to 16-byte rows for CPU<->GPU layout matching.

export const SHADER_CODE = /* wgsl */ `

const MAX_DEPTH: u32 = 4u;
const MAX_SPHERES: u32 = 8u;
const MAX_CUBES: u32 = 8u;
const MAX_PLANES: u32 = 4u;
const MAX_LIGHTS: u32 = 4u;
const INFINITY: f32 = 1.0e20;
const EPS: f32 = 1.0e-4;

// 80 bytes (20 floats, 5 rows)
struct Sphere {
  row0: vec4<f32>, // center.xyz, radius
  row1: vec4<f32>, // albedo.rgb, 0
  row2: vec4<f32>, // emission.rgb, 0
  row3: vec4<f32>, // reflectivity, refractivity, ior, 0
  row4: vec4<f32>, // padding
};

// 80 bytes
struct Cube {
  row0: vec4<f32>, // min.xyz, 0
  row1: vec4<f32>, // max.xyz, 0
  row2: vec4<f32>, // albedo.rgb, 0
  row3: vec4<f32>, // emission.rgb, 0
  row4: vec4<f32>, // reflectivity, refractivity, ior, 0
};

// 80 bytes
struct Plane {
  row0: vec4<f32>, // normal.xyz, d
  row1: vec4<f32>, // albedo.rgb, 0
  row2: vec4<f32>, // emission.rgb, 0
  row3: vec4<f32>, // reflectivity, refractivity, ior, 0
  row4: vec4<f32>, // padding
};

// 48 bytes
struct Light {
  row0: vec4<f32>, // type, 0, 0, 0
  row1: vec4<f32>, // pos.xyz, 0
  row2: vec4<f32>, // color.rgb, intensity
};

// 64 bytes
struct Camera {
  origin: vec4<f32>, // origin.xyz, 0
  right: vec4<f32>,  // right.xyz, 0
  up: vec4<f32>,     // up.xyz, 0
  forward: vec4<f32>,// forward.xyz, 0
  extras: vec4<f32>, // fovScale, aspect, 0, 0
};

// 96 bytes (6 x vec4)
struct Triangle {
  v0: vec4<f32>,     // xyz, nx (face normal x)
  v1: vec4<f32>,     // xyz, ny
  v2: vec4<f32>,     // xyz, nz
  albedoEmit: vec4<f32>, // albedo.rgb, emission.r
  rest: vec4<f32>,   // emission.gb, reflectivity, refractivity
  ior: vec4<f32>,    // ior, pad, pad, pad
};

// 48 bytes (3 x vec4)
struct BVHNode {
  bboxMin: vec4<f32>,  // xyz, 0
  bboxMax: vec4<f32>,  // xyz, 0
  data: vec4<u32>,     // leftFirst, triCount, 0, 0
};

// 64 bytes
struct SceneUniforms {
  ambient: vec4<f32>,    // ambient.rgb, 0
  background: vec4<f32>, // background.rgb, 0
  counts: vec4<u32>,     // numS, numC, numP, numL
  extras: vec4<u32>,     // frameCount, numTris, debugBVH, 0
};

@group(0) @binding(0) var<storage, read> spheres: array<Sphere, MAX_SPHERES>;
@group(0) @binding(1) var<storage, read> cubes: array<Cube, MAX_CUBES>;
@group(0) @binding(2) var<storage, read> planes: array<Plane, MAX_PLANES>;
@group(0) @binding(3) var<storage, read> lights: array<Light, MAX_LIGHTS>;
@group(0) @binding(4) var<uniform> camera: Camera;
@group(0) @binding(5) var<uniform> scene: SceneUniforms;
@group(0) @binding(6) var<storage, read_write> accum: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read_write> output: array<vec4<f32>>;
@group(0) @binding(8) var<storage, read> triangles: array<Triangle>;
@group(0) @binding(9) var<storage, read> bvhNodes: array<BVHNode>;

fn sphere_center(s: Sphere) -> vec3<f32> { return s.row0.xyz; }
fn sphere_radius(s: Sphere) -> f32 { return s.row0.w; }
fn sphere_albedo(s: Sphere) -> vec3<f32> { return s.row1.xyz; }
fn sphere_emission(s: Sphere) -> vec3<f32> { return s.row2.xyz; }
fn sphere_refl(s: Sphere) -> f32 { return s.row3.x; }
fn sphere_refr(s: Sphere) -> f32 { return s.row3.y; }
fn sphere_ior(s: Sphere) -> f32 { return s.row3.z; }

fn cube_min(c: Cube) -> vec3<f32> { return c.row0.xyz; }
fn cube_max(c: Cube) -> vec3<f32> { return c.row1.xyz; }
fn cube_albedo(c: Cube) -> vec3<f32> { return c.row2.xyz; }
fn cube_emission(c: Cube) -> vec3<f32> { return c.row3.xyz; }
fn cube_refl(c: Cube) -> f32 { return c.row4.x; }
fn cube_refr(c: Cube) -> f32 { return c.row4.y; }
fn cube_ior(c: Cube) -> f32 { return c.row4.z; }

fn plane_normal(p: Plane) -> vec3<f32> { return p.row0.xyz; }
fn plane_d(p: Plane) -> f32 { return p.row0.w; }
fn plane_albedo(p: Plane) -> vec3<f32> { return p.row1.xyz; }
fn plane_emission(p: Plane) -> vec3<f32> { return p.row2.xyz; }
fn plane_refl(p: Plane) -> f32 { return p.row3.x; }
fn plane_refr(p: Plane) -> f32 { return p.row3.y; }
fn plane_ior(p: Plane) -> f32 { return p.row3.z; }

fn light_type(l: Light) -> u32 { return u32(l.row0.x); }
fn light_pos(l: Light) -> vec3<f32> { return l.row1.xyz; }
fn light_color(l: Light) -> vec3<f32> { return l.row2.xyz; }
fn light_intensity(l: Light) -> f32 { return l.row2.w; }

fn camera_origin() -> vec3<f32> { return camera.origin.xyz; }
fn camera_right() -> vec3<f32> { return camera.right.xyz; }
fn camera_up() -> vec3<f32> { return camera.up.xyz; }
fn camera_forward() -> vec3<f32> { return camera.forward.xyz; }
fn camera_fovScale() -> f32 { return camera.extras.x; }
fn camera_aspect() -> f32 { return camera.extras.y; }

fn scene_ambient() -> vec3<f32> { return scene.ambient.xyz; }
fn scene_background() -> vec3<f32> { return scene.background.xyz; }
fn scene_numS() -> u32 { return scene.counts.x; }
fn scene_numC() -> u32 { return scene.counts.y; }
fn scene_numP() -> u32 { return scene.counts.z; }
fn scene_numL() -> u32 { return scene.counts.w; }
fn scene_frameCount() -> u32 { return scene.extras.x; }
fn scene_numTris() -> u32 { return scene.extras.y; }
fn scene_debugBVH() -> bool { return scene.extras.z != 0u; }

fn rand(seed: ptr<function, u32>) -> f32 {
  var x = *seed;
  x ^= x << 13u;
  x ^= x >> 17u;
  x ^= x << 5u;
  *seed = x;
  return f32(x & 0x00FFFFFFu) / f32(0x01000000u);
}

struct Hit {
  t: f32,
  p: vec3<f32>,
  n: vec3<f32>,
  albedo: vec3<f32>,
  emission: vec3<f32>,
  reflectivity: f32,
  refractivity: f32,
  ior: f32,
  hit: bool,
  bvhDepth: u32,
};

fn emptyHit() -> Hit {
  var h: Hit;
  h.t = INFINITY;
  h.p = vec3<f32>(0.0);
  h.n = vec3<f32>(0.0, 1.0, 0.0);
  h.albedo = vec3<f32>(0.0);
  h.emission = vec3<f32>(0.0);
  h.reflectivity = 0.0;
  h.refractivity = 0.0;
  h.ior = 1.0;
  h.hit = false;
  h.bvhDepth = 0u;
  return h;
}

fn tri_v0(t: Triangle) -> vec3<f32> { return t.v0.xyz; }
fn tri_v1(t: Triangle) -> vec3<f32> { return t.v1.xyz; }
fn tri_v2(t: Triangle) -> vec3<f32> { return t.v2.xyz; }
fn tri_normal(t: Triangle) -> vec3<f32> { return vec3<f32>(t.v0.w, t.v1.w, t.v2.w); }
fn tri_albedo(t: Triangle) -> vec3<f32> { return t.albedoEmit.xyz; }
fn tri_emission(t: Triangle) -> vec3<f32> { return vec3<f32>(t.albedoEmit.w, t.rest.x, t.rest.y); }
fn tri_refl(t: Triangle) -> f32 { return t.rest.z; }
fn tri_refr(t: Triangle) -> f32 { return t.rest.w; }
fn tri_ior(t: Triangle) -> f32 { return t.ior.x; }

fn bvh_leftFirst(n: BVHNode) -> u32 { return n.data.x; }
fn bvh_triCount(n: BVHNode) -> u32 { return n.data.y; }

fn intersectSphere(ro: vec3<f32>, rd: vec3<f32>, tMax: f32, s: Sphere, h: ptr<function, Hit>) {
  let oc = ro - sphere_center(s);
  let b = dot(oc, rd);
  let c = dot(oc, oc) - sphere_radius(s) * sphere_radius(s);
  let disc = b * b - c;
  if (disc < 0.0) { return; }
  let sq = sqrt(disc);
  var t = -b - sq;
  if (t < EPS) { t = -b + sq; }
  if (t < EPS || t >= tMax) { return; }
  let p = ro + rd * t;
  let n = normalize(p - sphere_center(s));
  (*h).t = t;
  (*h).p = p;
  (*h).n = n;
  (*h).albedo = sphere_albedo(s);
  (*h).emission = sphere_emission(s);
  (*h).reflectivity = sphere_refl(s);
  (*h).refractivity = sphere_refr(s);
  (*h).ior = sphere_ior(s);
  (*h).hit = true;
}

fn intersectPlane(ro: vec3<f32>, rd: vec3<f32>, tMax: f32, pl: Plane, h: ptr<function, Hit>) {
  let nrm = plane_normal(pl);
  let denom = dot(nrm, rd);
  if (abs(denom) < 1.0e-6) { return; }
  let t = -(dot(nrm, ro) + plane_d(pl)) / denom;
  if (t < EPS || t >= tMax) { return; }
  let p = ro + rd * t;
  var n = nrm;
  if (denom > 0.0) { n = -n; }
  (*h).t = t;
  (*h).p = p;
  (*h).n = n;
  (*h).albedo = plane_albedo(pl);
  (*h).emission = plane_emission(pl);
  (*h).reflectivity = plane_refl(pl);
  (*h).refractivity = plane_refr(pl);
  (*h).ior = plane_ior(pl);
  (*h).hit = true;
}

fn intersectCube(ro: vec3<f32>, rd: vec3<f32>, tMax: f32, cb: Cube, h: ptr<function, Hit>) {
  let cbmin = cube_min(cb);
  let cbmax = cube_max(cb);
  let invD = 1.0 / rd;
  let t0 = (cbmin - ro) * invD;
  let t1 = (cbmax - ro) * invD;
  let tmin3 = min(t0, t1);
  let tmax3 = max(t0, t1);
  let tmin = max(max(tmin3.x, tmin3.y), tmin3.z);
  let tmax = min(min(tmax3.x, tmax3.y), tmax3.z);
  if (tmax < max(tmin, 0.0)) { return; }
  var t = tmin;
  if (t < EPS) { t = tmax; }
  if (t < EPS || t >= tMax) { return; }
  let p = ro + rd * t;
  var n = vec3<f32>(0.0);
  let e = 1.0e-3 * (1.0 + abs(p));
  if (abs(p.x - cbmin.x) < e.x) { n.x = -1.0; }
  else if (abs(p.x - cbmax.x) < e.x) { n.x = 1.0; }
  else if (abs(p.y - cbmin.y) < e.y) { n.y = -1.0; }
  else if (abs(p.y - cbmax.y) < e.y) { n.y = 1.0; }
  else if (abs(p.z - cbmin.z) < e.z) { n.z = -1.0; }
  else if (abs(p.z - cbmax.z) < e.z) { n.z = 1.0; }
  n = normalize(n);
  (*h).t = t;
  (*h).p = p;
  (*h).n = n;
  (*h).albedo = cube_albedo(cb);
  (*h).emission = cube_emission(cb);
  (*h).reflectivity = cube_refl(cb);
  (*h).refractivity = cube_refr(cb);
  (*h).ior = cube_ior(cb);
  (*h).hit = true;
}

// Möller-Trumbore triangle intersection
fn intersectTriangle(ro: vec3<f32>, rd: vec3<f32>, tMax: f32, t: Triangle, h: ptr<function, Hit>) {
  let v0 = tri_v0(t);
  let v1 = tri_v1(t);
  let v2 = tri_v2(t);
  let e1 = v1 - v0;
  let e2 = v2 - v0;
  let s = ro - v0;
  let s1 = cross(rd, e2);
  let denom = dot(s1, e1);
  if (abs(denom) < 1.0e-8) { return; }
  let inv = 1.0 / denom;
  let b1 = dot(s1, s) * inv;
  if (b1 < 0.0 || b1 > 1.0) { return; }
  let s2 = cross(s, e1);
  let b2 = dot(s2, rd) * inv;
  if (b2 < 0.0 || b1 + b2 > 1.0) { return; }
  let tHit = dot(s2, e2) * inv;
  if (tHit < EPS || tHit >= tMax) { return; }
  let p = ro + rd * tHit;
  // Face normal from triangle vertices (already stored per-triangle)
  let nrm = tri_normal(t);
  // Flip normal to face the ray
  var n = nrm;
  if (dot(rd, n) > 0.0) { n = -n; }
  (*h).t = tHit;
  (*h).p = p;
  (*h).n = n;
  (*h).albedo = tri_albedo(t);
  (*h).emission = tri_emission(t);
  (*h).reflectivity = tri_refl(t);
  (*h).refractivity = tri_refr(t);
  (*h).ior = tri_ior(t);
  (*h).hit = true;
}

// Ray-AABB intersection returning (hit, tNear)
fn intersectAABB(ro: vec3<f32>, invRd: vec3<f32>, bmin: vec3<f32>, bmax: vec3<f32>) -> vec2<bool, f32> {
  let t0 = (bmin - ro) * invRd;
  let t1 = (bmax - ro) * invRd;
  let tMin3 = min(t0, t1);
  let tMax3 = max(t0, t1);
  let tNear = max(max(tMin3.x, tMin3.y), tMin3.z);
  let tFar = min(min(tMax3.x, tMax3.y), tMax3.z);
  if (tFar < max(tNear, 0.0)) { return vec2<bool, f32>(false, 0.0); }
  return vec2<bool, f32>(true, max(tNear, 0.0));
}

// BVH traversal for triangle intersection (updates h in-place)
fn intersectBVH(ro: vec3<f32>, rd: vec3<f32>, h: ptr<function, Hit>) {
  if (scene_numTris() == 0u) { return; }

  let invRd = 1.0 / rd;

  // Fixed-size traversal stack storing (nodeIdx, depth) pairs.
  // For 1M triangles with LEAF_SIZE=2, depth < 24, so 64 slots suffice.
  var stack: array<u32, 64>;
  var sp = 0u;
  stack[0] = 0u; // root node
  stack[1] = 0u; // root depth = 0
  sp = 2u;

  var maxDepthSeen = 0u;

  while (sp > 0u) {
    sp -= 2u;
    let nodeIdx = stack[sp];
    let depth = stack[sp + 1u];
    let node = bvhNodes[nodeIdx];

    // Quick AABB test (skip whole subtree if ray misses the box)
    let aabbHit = intersectAABB(ro, invRd, node.bboxMin.xyz, node.bboxMax.xyz);
    if (!aabbHit.x) { continue; }
    // Also skip if the AABB's near distance is beyond current best hit
    if (aabbHit.y >= (*h).t) { continue; }

    let tc = bvh_triCount(node);

    if (tc > 0u) {
      // Leaf node: test all triangles
      let first = bvh_leftFirst(node);
      var ti = 0u;
      while (ti < tc) {
        intersectTriangle(ro, rd, (*h).t, triangles[first + ti], h);
        ti++;
      }
      if (depth > maxDepthSeen) { maxDepthSeen = depth; }
    } else {
      // Interior node: test both children, push farther first
      let leftIdx = bvh_leftFirst(node);
      let rightIdx = leftIdx + 1u;

      let hitL = intersectAABB(ro, invRd, bvhNodes[leftIdx].bboxMin.xyz, bvhNodes[leftIdx].bboxMax.xyz);
      let hitR = intersectAABB(ro, invRd, bvhNodes[rightIdx].bboxMin.xyz, bvhNodes[rightIdx].bboxMax.xyz);

      let childDepth = depth + 1u;
      if (hitL.x && hitR.x) {
        // Push farther child first so nearer is processed first
        if (hitL.y < hitR.y) {
          stack[sp] = rightIdx; stack[sp + 1u] = childDepth; sp += 2u;
          stack[sp] = leftIdx;  stack[sp + 1u] = childDepth; sp += 2u;
        } else {
          stack[sp] = leftIdx;  stack[sp + 1u] = childDepth; sp += 2u;
          stack[sp] = rightIdx; stack[sp + 1u] = childDepth; sp += 2u;
        }
      } else if (hitL.x) {
        stack[sp] = leftIdx; stack[sp + 1u] = childDepth; sp += 2u;
      } else if (hitR.x) {
        stack[sp] = rightIdx; stack[sp + 1u] = childDepth; sp += 2u;
      }
    }
  }

  if (maxDepthSeen > (*h).bvhDepth) {
    (*h).bvhDepth = maxDepthSeen;
  }
}

fn sceneIntersect(ro: vec3<f32>, rd: vec3<f32>) -> Hit {
  var best = emptyHit();
  var i = 0u;
  loop {
    if (i >= scene_numS()) { break; }
    intersectSphere(ro, rd, best.t, spheres[i], &best);
    i++;
  }
  i = 0u;
  loop {
    if (i >= scene_numC()) { break; }
    intersectCube(ro, rd, best.t, cubes[i], &best);
    i++;
  }
  i = 0u;
  loop {
    if (i >= scene_numP()) { break; }
    intersectPlane(ro, rd, best.t, planes[i], &best);
    i++;
  }
  intersectBVH(ro, rd, &best);
  return best;
}

fn sceneOccluded(ro: vec3<f32>, rd: vec3<f32>, maxT: f32) -> bool {
  var i = 0u;
  loop {
    if (i >= scene_numS()) { break; }
    let s = spheres[i];
    let oc = ro - sphere_center(s);
    let b = dot(oc, rd);
    let c = dot(oc, oc) - sphere_radius(s) * sphere_radius(s);
    let disc = b * b - c;
    if (disc >= 0.0) {
      let sq = sqrt(disc);
      var t = -b - sq;
      if (t < EPS) { t = -b + sq; }
      if (t >= EPS && t < maxT) { return true; }
    }
    i++;
  }
  i = 0u;
  loop {
    if (i >= scene_numC()) { break; }
    let cb = cubes[i];
    let invD = 1.0 / rd;
    let t0 = (cube_min(cb) - ro) * invD;
    let t1 = (cube_max(cb) - ro) * invD;
    let tmin3 = min(t0, t1);
    let tmax3 = max(t0, t1);
    let tmin = max(max(tmin3.x, tmin3.y), tmin3.z);
    let tmax = min(min(tmax3.x, tmax3.y), tmax3.z);
    if (tmax >= max(tmin, 0.0) && tmax < maxT) {
      var t = tmin;
      if (t < EPS) { t = tmax; }
      if (t >= EPS && t < maxT) { return true; }
    }
    i++;
  }
  i = 0u;
  loop {
    if (i >= scene_numP()) { break; }
    let pl = planes[i];
    let denom = dot(plane_normal(pl), rd);
    if (abs(denom) > 1.0e-6) {
      let t = -(dot(plane_normal(pl), ro) + plane_d(pl)) / denom;
      if (t >= EPS && t < maxT) { return true; }
    }
    i++;
  }
  // BVH occlusion test (early-exit on first hit)
  if (scene_numTris() > 0u) {
    let invRd = 1.0 / rd;
    var stack2: array<u32, 32>;
    var sTop = 0u;
    stack2[0] = 0u;
    sTop = 1u;
    while (sTop > 0u) {
      sTop--;
      let nIdx = stack2[sTop];
      let nd = bvhNodes[nIdx];
      let aHit = intersectAABB(ro, invRd, nd.bboxMin.xyz, nd.bboxMax.xyz);
      if (!aHit.x || aHit.y >= maxT) { continue; }
      let tc2 = bvh_triCount(nd);
      if (tc2 > 0u) {
        let first2 = bvh_leftFirst(nd);
        var ti = 0u;
        while (ti < tc2) {
          let t = triangles[first2 + ti];
          let v0 = tri_v0(t);
          let v1 = tri_v1(t);
          let v2 = tri_v2(t);
          let e1 = v1 - v0;
          let e2 = v2 - v0;
          let s = ro - v0;
          let s1 = cross(rd, e2);
          let denom = dot(s1, e1);
          if (abs(denom) >= 1.0e-8) {
            let inv = 1.0 / denom;
            let b1 = dot(s1, s) * inv;
            if (b1 >= 0.0 && b1 <= 1.0) {
              let s2 = cross(s, e1);
              let b2 = dot(s2, rd) * inv;
              if (b2 >= 0.0 && b1 + b2 <= 1.0) {
                let tHit = dot(s2, e2) * inv;
                if (tHit >= EPS && tHit < maxT) { return true; }
              }
            }
          }
          ti++;
        }
      } else {
        let lIdx = bvh_leftFirst(nd);
        let rIdx = lIdx + 1u;
        let hL = intersectAABB(ro, invRd, bvhNodes[lIdx].bboxMin.xyz, bvhNodes[lIdx].bboxMax.xyz);
        let hR = intersectAABB(ro, invRd, bvhNodes[rIdx].bboxMin.xyz, bvhNodes[rIdx].bboxMax.xyz);
        if (hL.x && hR.x) {
          stack2[sTop] = rIdx; sTop++;
          stack2[sTop] = lIdx; sTop++;
        } else if (hL.x) {
          stack2[sTop] = lIdx; sTop++;
        } else if (hR.x) {
          stack2[sTop] = rIdx; sTop++;
        }
      }
    }
  }
  return false;
}

fn fresnelSchlick(cosTheta: f32, iorFrom: f32, iorTo: f32) -> f32 {
  var r0 = (iorFrom - iorTo) / (iorFrom + iorTo);
  r0 = r0 * r0;
  let one = 1.0 - cosTheta;
  return clamp(r0 + (1.0 - r0) * one * one * one * one * one, 0.0, 1.0);
}

// Compute refracted direction using Snell's law.
fn refract(rd: vec3<f32>, n: vec3<f32>, iorFrom: f32, iorTo: f32) -> vec2<bool, vec3<f32>> {
  let eta = iorFrom / iorTo;
  let cosThetaI = clamp(dot(-rd, n), -1.0, 1.0);
  let sin2ThetaT = eta * eta * (1.0 - cosThetaI * cosThetaI);
  if (sin2ThetaT > 1.0) {
    return vec2<bool, vec3<f32>>(false, vec3<f32>(0.0));
  }
  let cosThetaT = sqrt(1.0 - sin2ThetaT);
  let T = normalize(eta * rd + (eta * cosThetaI - cosThetaT) * n);
  return vec2<bool, vec3<f32>>(true, T);
}

fn traceRecursive(ro: vec3<f32>, rd: vec3<f32>, depth: u32, throughput: vec3<f32>, seed: ptr<function, u32>) -> vec3<f32> {
  // --- Russian Roulette: terminate low-contribution paths early ---
  if (depth >= 2u) {
    let q = max(max(throughput.x, throughput.y), throughput.z);
    if (q < 0.25) {
      let p = clamp(q, 0.05, 1.0);
      if (rand(seed) > p) {
        return vec3<f32>(0.0);
      }
      throughput = throughput / p;
    }
  }

  if (depth > MAX_DEPTH) { return scene_background(); }
  let h = sceneIntersect(ro, rd);
  if (!h.hit) { return scene_background(); }

  // --- Debug BVH mode: color by BVH depth ---
  if (scene_debugBVH()) {
    let d = f32(h.bvhDepth) / 24.0;
    let r = d;
    let g = 1.0 - abs(d - 0.5) * 2.0;
    let b = 1.0 - d;
    return vec3<f32>(r, g, b);
  }

  // --- direct lighting with shadow rays ---
  var Ld = vec3<f32>(0.0);
  var i = 0u;
  loop {
    if (i >= scene_numL()) { break; }
    let light = lights[i];
    var toL: vec3<f32>;
    var maxT: f32;
    var intensity: f32;
    if (light_type(light) == 0u) {
      let d = light_pos(light) - h.p;
      let dist = length(d);
      toL = d / dist;
      maxT = dist - 1.0e-3;
      intensity = light_intensity(light) / (1.0 + dist * dist);
    } else {
      toL = normalize(-light_pos(light));
      maxT = 1.0e8;
      intensity = light_intensity(light);
    }
    let ndl = max(dot(h.n, toL), 0.0);
    if (ndl > 0.0) {
      let origin = h.p + h.n * 2.0 * EPS;
      if (!sceneOccluded(origin, toL, maxT)) {
        Ld = Ld + light_color(light) * intensity * ndl;
      }
    }
    i++;
  }
  var color = h.emission + (Ld + scene_ambient()) * h.albedo;

  if (depth < MAX_DEPTH) {
    var nFresnel = h.n;
    var iorFrom = 1.0;
    var iorTo = h.ior;
    if (dot(rd, h.n) > 0.0) {
      nFresnel = -h.n;
      iorFrom = h.ior;
      iorTo = 1.0;
    }

    var Kr = h.reflectivity;
    var Kt = h.refractivity;

    if (Kt > 0.0) {
      let cosThetaI = clamp(dot(-rd, nFresnel), 0.0, 1.0);
      let F = fresnelSchlick(cosThetaI, iorFrom, iorTo);
      Kr = max(Kr, F * Kt);
      Kt = Kt * (1.0 - F);
    }

    // Reflection ray
    if (Kr > 0.0) {
      let rr = normalize(reflect(rd, h.n));
      let ro2 = h.p + h.n * 2.0 * EPS;
      let rc = traceRecursive(ro2, rr, depth + 1u, throughput * Kr, seed);
      color = color + Kr * rc;
    }

    // Refraction ray
    if (Kt > 0.0) {
      let res = refract(rd, nFresnel, iorFrom, iorTo);
      if (res.x) {
        let tr = res.y;
        let ro2 = h.p - nFresnel * 2.0 * EPS;
        let tc = traceRecursive(ro2, tr, depth + 1u, throughput * Kt, seed);
        color = color + Kt * tc;
      } else {
        // Total internal reflection
        let rr = normalize(reflect(rd, h.n));
        let ro2 = h.p + h.n * 2.0 * EPS;
        let rc = traceRecursive(ro2, rr, depth + 1u, throughput * Kt, seed);
        color = color + Kt * rc;
      }
    }
  }

  return color;
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let x = gid.x;
  let y = gid.y;
  if (x >= 800u || y >= 600u) { return; }
  let idx = y * 800u + x;

  var seed = (x * 1973u + y * 9277u) ^ (scene_frameCount() * 26699u) ^ 0x9E3779B9u;

  var u = (f32(x) + 0.5) / 800.0;
  var v = (f32(y) + 0.5) / 600.0;
  let jx = (rand(&seed) - 0.5) / 800.0;
  let jy = (rand(&seed) - 0.5) / 600.0;
  u = u + jx;
  v = v + jy;

  let ndcX = (2.0 * u - 1.0) * camera_aspect() * camera_fovScale();
  let ndcY = (1.0 - 2.0 * v) * camera_fovScale();

  let ro = camera_origin();
  let rd = normalize(camera_forward() + ndcX * camera_right() + ndcY * camera_up());

  let color = traceRecursive(ro, rd, 0u, vec3<f32>(1.0), &seed);

  var acc = accum[idx];
  acc = acc + vec4<f32>(color, 1.0);
  accum[idx] = acc;

  var avg = acc.rgb / max(acc.a, 1.0);
  avg = avg / (1.0 + avg);
  avg = pow(avg, vec3<f32>(1.0 / 2.2));
  output[idx] = vec4<f32>(avg, 1.0);
}
`;
