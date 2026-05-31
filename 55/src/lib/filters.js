// 视频滤镜处理模块
// 所有滤镜都基于 ImageData 的像素级操作，可在主线程预览和 Worker 转码中复用
// 设计原则：避免分配临时大对象，复用 buffer，保证速度

export const FILTER_TYPES = {
  GRAYSCALE: 'grayscale',
  EDGE: 'edge',
  BLUR: 'blur',
  BRIGHTNESS_CONTRAST: 'brightness_contrast',
}

export function createDefaultFilters() {
  return [
    { type: FILTER_TYPES.GRAYSCALE, enabled: false },
    { type: FILTER_TYPES.EDGE, enabled: false, threshold: 50 },
    { type: FILTER_TYPES.BLUR, enabled: false, radius: 3 },
    {
      type: FILTER_TYPES.BRIGHTNESS_CONTRAST,
      enabled: false,
      brightness: 0,
      contrast: 0,
    },
  ]
}

// --- 单个滤镜实现 ---

// 灰度化
export function applyGrayscale(data) {
  const d = data
  for (let i = 0; i < d.length; i += 4) {
    const l = (d[i] * 299 + d[i + 1] * 587 + d[i + 2] * 114) / 1000
    d[i] = d[i + 1] = d[i + 2] = l
  }
  return data
}

// 亮度对比度
// brightness: -100 ~ 100, contrast: -100 ~ 100
export function applyBrightnessContrast(data, brightness, contrast) {
  const b = Math.round(brightness * 2.55)
  const c = contrast
  const factor = (259 * (c + 255)) / (255 * (259 - c))
  const d = data
  for (let i = 0; i < d.length; i += 4) {
    let r = d[i]
    let g = d[i + 1]
    let bl = d[i + 2]
    r = factor * (r - 128) + 128 + b
    g = factor * (g - 128) + 128 + b
    bl = factor * (bl - 128) + 128 + b
    d[i] = r < 0 ? 0 : r > 255 ? 255 : r
    d[i + 1] = g < 0 ? 0 : g > 255 ? 255 : g
    d[i + 2] = bl < 0 ? 0 : bl > 255 ? 255 : bl
  }
  return data
}

// 高斯模糊（半径 1~5），使用分离卷积
export function applyGaussianBlur(data, width, height, radius) {
  radius = Math.max(1, Math.min(5, Math.round(radius)))
  const size = radius * 2 + 1
  const kernel = makeGaussianKernel(size)
  const tmp = new Uint8ClampedArray(data.length)
  // 水平
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let r = 0, g = 0, b = 0, a = 0
      for (let k = 0; k < size; k++) {
        const px = Math.min(width - 1, Math.max(0, x - radius + k))
        const idx = (y * width + px) * 4
        const w = kernel[k]
        r += data[idx] * w
        g += data[idx + 1] * w
        b += data[idx + 2] * w
        a += data[idx + 3] * w
      }
      const o = (y * width + x) * 4
      tmp[o] = r
      tmp[o + 1] = g
      tmp[o + 2] = b
      tmp[o + 3] = a
    }
  }
  // 垂直
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let r = 0, g = 0, b = 0, a = 0
      for (let k = 0; k < size; k++) {
        const py = Math.min(height - 1, Math.max(0, y - radius + k))
        const idx = (py * width + x) * 4
        const w = kernel[k]
        r += tmp[idx] * w
        g += tmp[idx + 1] * w
        b += tmp[idx + 2] * w
        a += tmp[idx + 3] * w
      }
      const o = (y * width + x) * 4
      data[o] = r
      data[o + 1] = g
      data[o + 2] = b
      data[o + 3] = a
    }
  }
  return data
}

function makeGaussianKernel(size) {
  const sigma = size / 4
  const half = (size - 1) / 2
  const kernel = new Float32Array(size)
  let sum = 0
  for (let i = 0; i < size; i++) {
    const x = i - half
    const v = Math.exp(-(x * x) / (2 * sigma * sigma))
    kernel[i] = v
    sum += v
  }
  for (let i = 0; i < size; i++) kernel[i] /= sum
  return kernel
}

// 边缘检测（Sobel）
export function applyEdgeDetect(data, width, height, threshold) {
  const gray = new Uint8ClampedArray(width * height)
  for (let i = 0, j = 0; i < data.length; i += 4, j++) {
    gray[j] = (data[i] * 299 + data[i + 1] * 587 + data[i + 2] * 114) / 1000
  }
  const threshold2 = Math.max(0, Math.min(255, threshold))
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const xm = x === 0 ? 0 : x - 1
      const xp = x === width - 1 ? x : x + 1
      const ym = y === 0 ? 0 : y - 1
      const yp = y === height - 1 ? y : y + 1
      const tl = gray[ym * width + xm]
      const tc = gray[ym * width + x]
      const tr = gray[ym * width + xp]
      const ml = gray[y * width + xm]
      const mr = gray[y * width + xp]
      const bl = gray[yp * width + xm]
      const bc = gray[yp * width + x]
      const br = gray[yp * width + xp]
      const gx = -tl - 2 * ml - bl + tr + 2 * mr + br
      const gy = -tl - 2 * tc - tr + bl + 2 * bc + br
      const mag = Math.sqrt(gx * gx + gy * gy)
      const v = mag > threshold2 ? 255 : 0
      const o = (y * width + x) * 4
      data[o] = v
      data[o + 1] = v
      data[o + 2] = v
      // 保留 alpha
    }
  }
  return data
}

// --- 滤镜链 ---
// filters: 按启用顺序排列的滤镜数组
// data: Uint8ClampedArray（ImageData.data）
// width, height: 图像尺寸
export function applyFilterChain(filters, data, width, height) {
  for (const f of filters) {
    if (!f.enabled) continue
    switch (f.type) {
      case FILTER_TYPES.GRAYSCALE:
        applyGrayscale(data)
        break
      case FILTER_TYPES.BRIGHTNESS_CONTRAST:
        applyBrightnessContrast(data, f.brightness || 0, f.contrast || 0)
        break
      case FILTER_TYPES.BLUR:
        applyGaussianBlur(data, width, height, f.radius || 3)
        break
      case FILTER_TYPES.EDGE:
        applyEdgeDetect(data, width, height, f.threshold || 50)
        break
    }
  }
  return data
}

// 工具：将 VideoFrame 绘制到 canvas 并获取 ImageData，应用滤镜后再 putImageData
// 这是 Worker 中使用的入口
export function processFrameInPlace(ctx, width, height, filters) {
  if (!filters || filters.every((f) => !f.enabled)) return
  const imgData = ctx.getImageData(0, 0, width, height)
  applyFilterChain(filters, imgData.data, width, height)
  ctx.putImageData(imgData, 0, 0)
}
