import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import Stats from 'stats.js';

const MIN_COUNT = 100_000;
const MAX_COUNT = 1_000_000;
const DEFAULT_COUNT = 1_000_000;

const vertexShader = /* glsl */ `
  precision highp float;

  uniform float uTime;
  uniform float uNoiseScale;
  uniform float uSpeed;
  uniform float uSize;

  uniform vec3  uMouseWorld;
  uniform float uMouseStrength;
  uniform float uMouseRadius;

  uniform vec3  uPulseWorld;
  uniform float uPulseTime;
  uniform float uPulseDuration;
  uniform float uPulseStrength;

  attribute vec3 seedOffset;
  attribute float seedScale;

  varying vec3 vColor;

  // Ashima / Ian McEwan / Stefan Gustavson 3D simplex noise
  vec3 mod289(vec3 x){ return x - floor(x * (1.0 / 289.0)) * 289.0; }
  vec4 mod289(vec4 x){ return x - floor(x * (1.0 / 289.0)) * 289.0; }
  vec4 permute(vec4 x){ return mod289(((x*34.0)+1.0)*x); }
  vec4 taylorInvSqrt(vec4 r){ return 1.79284291400159 - 0.85373472095314 * r; }

  float snoise(vec3 v){
    const vec2 C = vec2(1.0/6.0, 1.0/3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);
    vec3 i  = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);
    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;
    i = mod289(i);
    vec4 p = permute( permute( permute(
               i.z + vec4(0.0, i1.z, i2.z, 1.0 ))
             + i.y + vec4(0.0, i1.y, i2.y, 1.0 ))
             + i.x + vec4(0.0, i1.x, i2.x, 1.0 ));
    float n_ = 0.142857142857;
    vec3 ns = n_ * D.wyz - D.xzx;
    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    vec4 x = x_ *ns.x + ns.yyyy;
    vec4 y = y_ *ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    vec4 s0 = floor(b0)*2.0 + 1.0;
    vec4 s1 = floor(b1)*2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    vec4 a0 = b0.xzyw + s0.xzyw*sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw*sh.zzww;
    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);
    vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2,p2), dot(p3,p3)));
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m*m, vec4(dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3)));
  }

  vec3 curlField(vec3 p){
    const float e = 0.15;
    vec3 dx = vec3(e, 0.0, 0.0);
    vec3 dy = vec3(0.0, e, 0.0);
    vec3 dz = vec3(0.0, 0.0, e);
    float n1 = snoise(p + dy) - snoise(p - dy);
    float n2 = snoise(p + dz) - snoise(p - dz);
    float n3 = snoise(p + dx) - snoise(p - dx);
    float n4 = snoise(p + dz) - snoise(p - dz);
    float n5 = snoise(p + dx) - snoise(p - dx);
    float n6 = snoise(p + dy) - snoise(p - dy);
    return vec3(n1 - n2, n3 - n4, n5 - n6) * (0.5 / e);
  }

  vec3 speedColor(float s){
    vec3 cLo = vec3(0.15, 0.40, 1.00);
    vec3 cMid = vec3(0.25, 1.00, 0.45);
    vec3 cHi = vec3(1.00, 0.20, 0.15);
    float t = clamp(s, 0.0, 1.0);
    if (t < 0.5) {
      return mix(cLo, cMid, t * 2.0);
    } else {
      return mix(cMid, cHi, (t - 0.5) * 2.0);
    }
  }

  void main(){
    float t = uTime * uSpeed * 0.35;
    vec3 base = position + seedOffset * 0.01;
    vec3 np = base * uNoiseScale;

    vec3 flow = curlField(np + vec3(t * 0.7, t * 0.9, t * 1.1));

    vec3 velocity = flow * seedScale * 2.5;

    // Mouse attraction (pulls particles toward the cursor in world space)
    // Use worldPos = position (base) so the attraction is stable and smooth.
    float mDist = length(position - uMouseWorld);
    float mFalloff = 1.0 - smoothstep(0.0, uMouseRadius, mDist);
    vec3  mDir = normalize(uMouseWorld - position + 1e-5);
    velocity += mDir * uMouseStrength * mFalloff * mFalloff;

    // Click repulse pulse (expanding ring)
    if (uPulseTime > 0.0) {
      float pAge  = uTime - uPulseTime;
      float pT    = clamp(pAge / uPulseDuration, 0.0, 1.0);
      float pEnv  = (1.0 - pT) * (1.0 - pT);
      float pRing = pT * uPulseStrength;
      float pDist = distance(position, uPulseWorld);
      float pBand = exp(-abs(pDist - pRing) * 6.0);
      vec3  pDir  = normalize(position - uPulseWorld + 1e-5);
      velocity += pDir * uPulseStrength * pBand * pEnv;
    }

    float speed = clamp(length(velocity) * 0.25, 0.0, 1.0);

    vec3 worldPos = position + velocity * 0.6;

    vColor = speedColor(speed);

    vec4 mvPosition = modelViewMatrix * vec4(worldPos, 1.0);
    gl_Position = projectionMatrix * mvPosition;
    gl_PointSize = uSize * (300.0 / -mvPosition.z);
  }
`;

const fragmentShader = /* glsl */ `
  precision highp float;
  varying vec3 vColor;
  void main(){
    vec2 uv = gl_PointCoord - 0.5;
    float d = length(uv);
    float a = smoothstep(0.5, 0.0, d);
    if (a < 0.01) discard;
    gl_FragColor = vec4(vColor, a);
  }
`;

function mulberry32(a: number) {
  return function () {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

interface AppState {
  renderer: THREE.WebGLRenderer;
  scene: THREE.Scene;
  camera: THREE.PerspectiveCamera;
  controls: OrbitControls;
  points: THREE.Points;
  material: THREE.ShaderMaterial;
  geometry: THREE.BufferGeometry;
  clock: THREE.Clock;
  stats: Stats;
  countLabel: HTMLElement;
  countValue: HTMLElement;
  rebuildRequested: boolean;
  pendingCount: number;
  currentCount: number;
  resetRequested: boolean;
  baseTime: number;
  lastTime: number;
  mouseWorld: THREE.Vector3;
  mouseStrength: number;
  mouseRadius: number;
  mouseActive: boolean;
  pulseTime: number;
  pulseWorld: THREE.Vector3;
  pulseStrength: number;
  pulseDuration: number;
}

export function initParticleApp(canvas: HTMLCanvasElement) {
  const stats = new Stats();
  stats.showPanel(0);
  stats.dom.style.position = 'fixed';
  stats.dom.style.top = '60px';
  stats.dom.style.left = '10px';
  document.body.appendChild(stats.dom);

  const controls = document.createElement('div');
  controls.className = 'controls-panel';
  controls.innerHTML = `
    <h3>Particle System</h3>
    <div class="row">
      <label>Particle count <span class="val" id="countValue">1,000,000</span></label>
      <input id="countSlider" type="range" min="${MIN_COUNT}" max="${MAX_COUNT}" step="10000" value="${DEFAULT_COUNT}" />
    </div>
    <div class="row">
      <label>Speed <span class="val" id="speedValue">1.00</span></label>
      <input id="speedSlider" type="range" min="0.1" max="3" step="0.05" value="1" />
    </div>
    <div class="row">
      <label>Noise scale <span class="val" id="noiseValue">0.05</span></label>
      <input id="noiseSlider" type="range" min="0.01" max="0.2" step="0.005" value="0.05" />
    </div>
    <div class="row">
      <label>Point size <span class="val" id="sizeValue">2.0</span></label>
      <input id="sizeSlider" type="range" min="0.5" max="8" step="0.1" value="2" />
    </div>
    <div class="row">
      <label>Attraction radius <span class="val" id="radiusValue">50</span></label>
      <input id="radiusSlider" type="range" min="10" max="150" step="1" value="50" />
    </div>
    <div class="row">
      <label>Attraction strength <span class="val" id="attractValue">15</span></label>
      <input id="attractSlider" type="range" min="0" max="50" step="0.5" value="15" />
    </div>
    <button id="resetBtn" type="button">Reset motion</button>
  `;
  document.body.appendChild(controls);

  const btnStyle = document.createElement('style');
  btnStyle.textContent = `
    #resetBtn {
      width: 100%;
      margin-top: 6px;
      padding: 8px 12px;
      border: 1px solid #446;
      border-radius: 4px;
      background: linear-gradient(180deg,#2a3350,#1a2038);
      color: #cfe;
      cursor: pointer;
      font-size: 13px;
    }
    #resetBtn:hover { background: linear-gradient(180deg,#3a4570,#243050); }
    #resetBtn:active { transform: translateY(1px); }
  `;
  document.head.appendChild(btnStyle);

  const countLabel = document.createElement('div');
  countLabel.className = 'stats-panel';
  countLabel.innerHTML = `<div><span class="label">Particles:</span> <span class="value" id="particleCountLabel">1,000,000</span></div>`;
  document.body.appendChild(countLabel);

  const hint = document.createElement('div');
  hint.className = 'hint';
  hint.textContent = 'Drag to rotate · Scroll to zoom · Move to attract · Click to repulse · R to reset';
  document.body.appendChild(hint);

  const renderer = new THREE.WebGLRenderer({
    canvas,
    antialias: true,
    powerPreference: 'high-performance',
    stencil: false,
  });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.setClearColor(0x05070d, 1);

  const gl = renderer.getContext() as WebGL2RenderingContext | null;
  if (gl) {
    const SAMPLE_ALPHA_TO_COVERAGE = 0x809e;
    try {
      const samples = gl.getParameter(gl.SAMPLES);
      if (samples > 1) gl.enable(SAMPLE_ALPHA_TO_COVERAGE);
      console.debug('MSAA samples:', samples);
    } catch { /* no-op */ }
  }

  const scene = new THREE.Scene();
  scene.fog = new THREE.FogExp2(0x05070d, 0.005);

  const camera = new THREE.PerspectiveCamera(60, window.innerWidth / window.innerHeight, 0.1, 1000);
  camera.position.set(80, 60, 120);

  const orbit = new OrbitControls(camera, canvas);
  orbit.enableDamping = true;
  orbit.dampingFactor = 0.08;
  orbit.minDistance = 30;
  orbit.maxDistance = 400;

  const { material, geometry } = buildParticleSystem(DEFAULT_COUNT);
  const points = new THREE.Points(geometry, material);
  points.frustumCulled = false;
  scene.add(points);

  const state: AppState = {
    renderer,
    scene,
    camera,
    controls: orbit,
    points,
    material,
    geometry,
    clock: new THREE.Clock(),
    stats,
    countLabel: countLabel.querySelector('#particleCountLabel')!,
    countValue: controls.querySelector('#countValue')!,
    rebuildRequested: false,
    pendingCount: DEFAULT_COUNT,
    currentCount: DEFAULT_COUNT,
    resetRequested: false,
    baseTime: 0,
    lastTime: 0,
    mouseWorld: new THREE.Vector3(0, 0, 0),
    mouseStrength: 15,
    mouseRadius: 50,
    mouseActive: false,
    pulseTime: -1,
    pulseWorld: new THREE.Vector3(0, 0, 0),
    pulseStrength: 200,
    pulseDuration: 1.2,
  };

  const countSlider = controls.querySelector('#countSlider') as HTMLInputElement;
  const speedSlider = controls.querySelector('#speedSlider') as HTMLInputElement;
  const noiseSlider = controls.querySelector('#noiseSlider') as HTMLInputElement;
  const sizeSlider = controls.querySelector('#sizeSlider') as HTMLInputElement;
  const radiusSlider = controls.querySelector('#radiusSlider') as HTMLInputElement;
  const attractSlider = controls.querySelector('#attractSlider') as HTMLInputElement;
  const resetBtn = controls.querySelector('#resetBtn') as HTMLButtonElement;

  const speedValue = controls.querySelector('#speedValue')!;
  const noiseValue = controls.querySelector('#noiseValue')!;
  const sizeValue = controls.querySelector('#sizeValue')!;
  const radiusValue = controls.querySelector('#radiusValue')!;
  const attractValue = controls.querySelector('#attractValue')!;

  countSlider.addEventListener('input', () => {
    const n = parseInt(countSlider.value, 10);
    state.pendingCount = n;
    state.rebuildRequested = true;
    state.countValue.textContent = n.toLocaleString();
  });

  speedSlider.addEventListener('input', () => {
    const v = parseFloat(speedSlider.value);
    material.uniforms.uSpeed.value = v;
    speedValue.textContent = v.toFixed(2);
  });

  noiseSlider.addEventListener('input', () => {
    const v = parseFloat(noiseSlider.value);
    material.uniforms.uNoiseScale.value = v;
    noiseValue.textContent = v.toFixed(3);
  });

  sizeSlider.addEventListener('input', () => {
    const v = parseFloat(sizeSlider.value);
    material.uniforms.uSize.value = v;
    sizeValue.textContent = v.toFixed(1);
  });

  radiusSlider.addEventListener('input', () => {
    const v = parseFloat(radiusSlider.value);
    state.mouseRadius = v;
    material.uniforms.uMouseRadius.value = v;
    radiusValue.textContent = v.toFixed(0);
  });

  attractSlider.addEventListener('input', () => {
    const v = parseFloat(attractSlider.value);
    state.mouseStrength = v;
    attractValue.textContent = v.toFixed(1);
  });

  resetBtn.addEventListener('click', () => {
    state.resetRequested = true;
  });

  window.addEventListener('keydown', (ev) => {
    if (ev.key === 'r' || ev.key === 'R') {
      state.resetRequested = true;
    }
  });

  // --- Mouse interaction -----------------------------------------------------
  const raycaster = new THREE.Raycaster();
  const mousePlane = new THREE.Plane(new THREE.Vector3(0, 0, 1), 0);
  const tmpPoint = new THREE.Vector3();

  function projectMouseToWorld(clientX: number, clientY: number): THREE.Vector3 {
    const rect = canvas.getBoundingClientRect();
    const x = ((clientX - rect.left) / rect.width) * 2 - 1;
    const y = -((clientY - rect.top) / rect.height) * 2 + 1;
    raycaster.setFromCamera(new THREE.Vector2(x, y), camera);
    // Project onto a plane through the particle cloud center, facing the camera
    const camDir = new THREE.Vector3();
    camera.getWorldDirection(camDir);
    mousePlane.setFromNormalAndCoplanarPoint(camDir, new THREE.Vector3(0, 0, 0));
    raycaster.ray.intersectPlane(mousePlane, tmpPoint);
    return tmpPoint.clone();
  }

  let mouseDownButton = -1;

  canvas.addEventListener('pointerdown', (ev) => {
    mouseDownButton = ev.button;
    if (ev.button === 0) {
      // Trigger a repulse pulse at the click position
      const world = projectMouseToWorld(ev.clientX, ev.clientY);
      state.pulseWorld.copy(world);
      state.pulseTime = state.lastTime + state.baseTime;
      material.uniforms.uPulseWorld.value.copy(world);
      material.uniforms.uPulseTime.value = state.pulseTime;
    }
  });

  canvas.addEventListener('pointerup', () => {
    mouseDownButton = -1;
  });

  canvas.addEventListener('pointermove', (ev) => {
    const world = projectMouseToWorld(ev.clientX, ev.clientY);
    state.mouseWorld.copy(world);
    material.uniforms.uMouseWorld.value.copy(world);
    // Disable attraction while user is dragging (so orbit/zoom feels clean)
    state.mouseActive = mouseDownButton === -1;
  });

  canvas.addEventListener('pointerleave', () => {
    state.mouseActive = false;
  });

  canvas.addEventListener('pointerenter', () => {
    if (mouseDownButton === -1) state.mouseActive = true;
  });

  window.addEventListener('resize', () => onResize(state));

  animate(state);
}

function buildParticleSystem(count: number): { material: THREE.ShaderMaterial; geometry: THREE.BufferGeometry } {
  const positions = new Float32Array(count * 3);
  const seedOffsets = new Float32Array(count * 3);
  const seedScales = new Float32Array(count);

  const rand = mulberry32(1337);

  const radius = 40;
  for (let i = 0; i < count; i++) {
    const i3 = i * 3;
    const u = rand();
    const v = rand();
    const theta = 2 * Math.PI * u;
    const phi = Math.acos(2 * v - 1);
    const r = radius * Math.cbrt(rand());

    positions[i3 + 0] = r * Math.sin(phi) * Math.cos(theta);
    positions[i3 + 1] = r * Math.sin(phi) * Math.sin(theta);
    positions[i3 + 2] = r * Math.cos(phi);

    seedOffsets[i3 + 0] = (rand() - 0.5) * 2000;
    seedOffsets[i3 + 1] = (rand() - 0.5) * 2000;
    seedOffsets[i3 + 2] = (rand() - 0.5) * 2000;

    seedScales[i] = 0.8 + rand() * 3.2;
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  geometry.setAttribute('seedOffset', new THREE.BufferAttribute(seedOffsets, 3));
  geometry.setAttribute('seedScale', new THREE.BufferAttribute(seedScales, 1));

  const material = new THREE.ShaderMaterial({
    vertexShader,
    fragmentShader,
    uniforms: {
      uTime: { value: 0 },
      uNoiseScale: { value: 0.05 },
      uSpeed: { value: 1.0 },
      uSize: { value: 2.0 },
      uMouseWorld: { value: new THREE.Vector3(0, 0, 0) },
      uMouseStrength: { value: 15.0 },
      uMouseRadius: { value: 50.0 },
      uPulseWorld: { value: new THREE.Vector3(0, 0, 0) },
      uPulseTime: { value: -1.0 },
      uPulseDuration: { value: 1.2 },
      uPulseStrength: { value: 200.0 },
    },
    transparent: true,
    depthWrite: false,
    blending: THREE.AdditiveBlending,
  });

  return { material, geometry };
}

function rebuildParticles(state: AppState, newCount: number) {
  state.geometry.dispose();

  const { geometry } = buildParticleSystem(newCount);
  state.geometry = geometry;
  state.points.geometry = geometry;
  state.points.material = state.material;

  state.currentCount = newCount;
  state.countLabel.textContent = newCount.toLocaleString();
}

function onResize(state: AppState) {
  const w = window.innerWidth;
  const h = window.innerHeight;
  state.renderer.setSize(w, h);
  state.camera.aspect = w / h;
  state.camera.updateProjectionMatrix();
}

function animate(state: AppState) {
  state.stats.begin();
  const delta = Math.min(state.clock.getDelta(), 0.05);

  if (state.rebuildRequested) {
    state.rebuildRequested = false;
    rebuildParticles(state, state.pendingCount);
  }

  if (state.resetRequested) {
    state.resetRequested = false;
    // Snap time back so the noise field resumes from t=0 (initial motion state)
    state.baseTime = state.lastTime;
    state.pulseTime = -1;
    state.material.uniforms.uPulseTime.value = -1.0;
  }

  state.lastTime += delta;
  const simTime = state.lastTime - state.baseTime;
  state.material.uniforms.uTime.value = simTime;

  // Smooth mouse strength transition (0 → target when mouse enters / not dragging)
  const targetStrength = state.mouseActive ? state.mouseStrength : 0;
  const strengthU = state.material.uniforms.uMouseStrength as { value: number };
  const lerp = 1 - Math.exp(-delta * 10);
  strengthU.value += (targetStrength - strengthU.value) * lerp;

  // Pulse auto-disable after duration
  if (state.pulseTime > 0) {
    const age = simTime - state.pulseTime;
    if (age > state.pulseDuration + 0.2) {
      state.pulseTime = -1;
      state.material.uniforms.uPulseTime.value = -1.0;
    }
  }

  state.controls.update();
  state.renderer.render(state.scene, state.camera);
  state.stats.end();

  requestAnimationFrame(() => animate(state));
}
