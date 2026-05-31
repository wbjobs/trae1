import { createDefaultScene, WIDTH, HEIGHT, type Scene } from './scene.js';
import { createDefaultCamera, computeCameraUniforms, type CameraState } from './camera.js';
import { initRenderer, type Renderer } from './renderer.js';
import { generateSampleModel, parseOBJ, type ParsedOBJ } from './obj.js';
import { buildBVH, type BVHResult } from './bvh.js';

const canvas = document.getElementById('canvas') as HTMLCanvasElement;
const hudFps = document.getElementById('fps') as HTMLElement;
const hudSpp = document.getElementById('sppVal') as HTMLElement;
const hudSmp = document.getElementById('smp') as HTMLElement;
const hudTri = document.getElementById('triVal') as HTMLElement;
const hudBvh = document.getElementById('bvhVal') as HTMLElement;
const sppInput = document.getElementById('spp') as HTMLInputElement;
const resetBtn = document.getElementById('reset') as HTMLInputElement;
const bvhBtn = document.getElementById('bvhToggle') as HTMLInputElement;
const objInput = document.getElementById('objFile') as HTMLInputElement;
const errorEl = document.getElementById('error') as HTMLDivElement;

canvas.width = WIDTH;
canvas.height = HEIGHT;

function showError(msg: string) {
  errorEl.style.display = 'flex';
  errorEl.textContent = msg;
}

async function main() {
  if (!('gpu' in navigator)) {
    showError('WebGPU is not available in this browser. Try Chrome 113+ with WebGPU enabled.');
    return;
  }
  const adapter = await (navigator as any).gpu.requestAdapter();
  if (!adapter) {
    showError('No GPU adapter found.');
    return;
  }
  const device = await adapter.requestDevice();
  device.lost.then((info: any) => {
    showError('GPU device lost: ' + info.message);
  });

  const scene: Scene = createDefaultScene();
  const cameraState: CameraState = createDefaultCamera();
  const aspect = WIDTH / HEIGHT;

  const renderer: Renderer = await initRenderer(device);
  renderer.updateScene(scene);
  renderer.updateCamera(computeCameraUniforms(cameraState, aspect));
  renderer.resetAccum();

  // Load default mesh (sample model with ~8000 triangles, 2048 cubes)
  let parsed: ParsedOBJ = generateSampleModel(2048);
  let bvh: BVHResult = buildBVH(parsed.triangles);

  const defaultMaterial = {
    albedo: [0.85, 0.75, 0.6] as [number, number, number],
    emission: [0.0, 0.0, 0.0] as [number, number, number],
    reflectivity: 0.15,
    refractivity: 0.0,
    ior: 1.5,
  };

  renderer.updateMesh(parsed.triangles, bvh, defaultMaterial);
  hudTri.textContent = parsed.triangles.length.toLocaleString();
  hudBvh.textContent = `N=${bvh.stats.nodeCount.toLocaleString()} D=${bvh.stats.maxDepth} (${bvh.stats.buildTimeMs.toFixed(0)}ms)`;

  // OBJ file loading
  objInput.addEventListener('change', (e) => {
    const file = (e.target as HTMLInputElement).files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (ev) => {
      const text = ev.target?.result as string;
      parsed = parseOBJ(text);
      if (parsed.triangles.length === 0) {
        showError('No triangles found in OBJ file.');
        return;
      }
      if (parsed.triangles.length > 1048576) {
        showError(`OBJ has ${parsed.triangles.length.toLocaleString()} triangles, exceeding the 1M limit.`);
        return;
      }
      const t0 = performance.now();
      bvh = buildBVH(parsed.triangles);
      const buildMs = performance.now() - t0;
      renderer.updateMesh(parsed.triangles, bvh, defaultMaterial);
      renderer.resetAccum();
      hudTri.textContent = parsed.triangles.length.toLocaleString();
      hudBvh.textContent = `N=${bvh.stats.nodeCount.toLocaleString()} D=${bvh.stats.maxDepth} (${buildMs.toFixed(0)}ms)`;
    };
    reader.readAsText(file);
  });

  // BVH debug toggle
  bvhBtn.addEventListener('click', () => {
    const on = !bvhBtn.classList.contains('active');
    bvhBtn.classList.toggle('active', on);
    bvhBtn.textContent = on ? 'BVH: ON' : 'BVH: OFF';
    renderer.setDebugBVH(on);
    renderer.resetAccum();
  });

  // WebGPU canvas context for zero-copy display.
  const ctx = (canvas as any).getContext('webgpu') as GPUCanvasContext;
  if (!ctx) {
    showError('WebGPU canvas context is unavailable.');
    return;
  }
  const format = (navigator as any).gpu.getPreferredCanvasFormat();
  ctx.configure({ device, format, alphaMode: 'opaque' });

  const blitModule = device.createShaderModule({
    code: /* wgsl */ `
    struct VSOut { @builtin(position) pos: vec4<f32>, @location(0) uv: vec2<f32> };
    @vertex
    fn vs(@builtin(vertex_index) i: u32) -> VSOut {
      var p = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0),
      );
      var uv = array<vec2<f32>, 3>(
        vec2<f32>(0.0, 1.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 0.0),
      );
      var o: VSOut;
      o.pos = vec4<f32>(p[i], 0.0, 1.0);
      o.uv = uv[i];
      return o;
    }
    @group(0) @binding(0) var<storage, read> pixels: array<vec4<f32>>;
    @fragment
    fn fs(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
      var px = u32(clamp(uv.x, 0.0, 0.9999) * 800.0);
      var py = u32(clamp(1.0 - uv.y, 0.0, 0.9999) * 600.0);
      return pixels[py * 800u + px];
    }
    `,
  });
  const blitPipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module: blitModule, entryPoint: 'vs' },
    fragment: { module: blitModule, entryPoint: 'fs', targets: [{ format }] },
    primitive: { topology: 'triangle-list' },
  });
  const blitBindGroup = device.createBindGroup({
    layout: blitPipeline.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: renderer.outputBuf } }],
  });

  let spp = parseInt(sppInput.value, 10) || 32;
  sppInput.addEventListener('input', () => {
    spp = parseInt(sppInput.value, 10) || 1;
    if (spp < 1) spp = 1;
    if (spp > 128) spp = 128;
    hudSpp.textContent = String(spp);
    renderer.resetAccum();
  });
  resetBtn.addEventListener('click', () => {
    Object.assign(cameraState, createDefaultCamera());
    renderer.updateCamera(computeCameraUniforms(cameraState, aspect));
    renderer.resetAccum();
  });

  let dragging = false;
  let lastX = 0, lastY = 0;
  canvas.addEventListener('mousedown', (e) => {
    dragging = true;
    lastX = e.clientX;
    lastY = e.clientY;
  });
  window.addEventListener('mouseup', () => { dragging = false; });
  window.addEventListener('mousemove', (e) => {
    if (!dragging) return;
    const dx = e.clientX - lastX;
    const dy = e.clientY - lastY;
    lastX = e.clientX;
    lastY = e.clientY;
    cameraState.yaw -= dx * 0.005;
    cameraState.pitch -= dy * 0.005;
    cameraState.pitch = Math.max(-Math.PI / 2 + 0.01, Math.min(Math.PI / 2 - 0.01, cameraState.pitch));
    renderer.updateCamera(computeCameraUniforms(cameraState, aspect));
    renderer.resetAccum();
  });
  canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    const factor = Math.exp(e.deltaY * 0.001);
    cameraState.distance = Math.max(1.5, Math.min(20.0, cameraState.distance * factor));
    renderer.updateCamera(computeCameraUniforms(cameraState, aspect));
    renderer.resetAccum();
  }, { passive: false });

  let lastT = performance.now();
  let frameTimeSmooth = 1000 / 10;

  function frame() {
    const now = performance.now();
    const dt = now - lastT;
    lastT = now;
    frameTimeSmooth = frameTimeSmooth * 0.9 + dt * 0.1;
    const fps = 1000 / frameTimeSmooth;

    const enc = device.createCommandEncoder();

    for (let i = 0; i < spp; i++) {
      renderer.dispatch(enc);
    }

    const pass = enc.beginRenderPass({
      colorAttachments: [
        {
          view: ctx.getCurrentTexture().createView(),
          clearValue: { r: 0, g: 0, b: 0, a: 1 },
          loadOp: 'clear',
          storeOp: 'store',
        },
      ],
    });
    pass.setPipeline(blitPipeline);
    pass.setBindGroup(0, blitBindGroup);
    pass.draw(3, 1, 0, 0);
    pass.end();

    device.queue.submit([enc.finish()]);

    hudFps.textContent = fps.toFixed(1);
    hudSmp.textContent = `${renderer.frameCount}`;

    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
}

main().catch((e) => showError(String(e)));
