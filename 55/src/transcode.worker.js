// 视频转码 Web Worker
// 分片按 30 秒读取并处理：解析 MP4 → 解码到 OffscreenCanvas → 滤镜 → MediaRecorder 录制 AV1 → 输出分片 Blob
// 支持内存监测：通过 performance.memory，超过阈值自动将分片从 30s 降到 15s / 10s

import { parseMp4Samples, findSampleRange } from './lib/mp4box.js'

let chunkDuration = 30
const MIN_CHUNK = 10

self.onmessage = async (e) => {
  const msg = e.data
  if (msg.type === 'start') {
    await runTranscode(msg.file, msg.options || {})
  }
}

async function runTranscode(file, options) {
  try {
    const filters = options.filters || []
    const anyFilter = filters.some((f) => f.enabled)

    const info = await parseMp4Samples(file)
    post({ type: 'meta', info })

    const targetHeight = Math.min(720, info.height)
    const scale = targetHeight / info.height
    const targetWidth = Math.round(info.width * scale / 2) * 2
    const targetBitrate = estimateBitrate(file, info) / 2
    const fps = estimateFps(info)

    post({
      type: 'params',
      width: targetWidth,
      height: targetHeight,
      bitrate: targetBitrate,
      fps,
      duration: info.totalDuration,
    })

    const canvas = new OffscreenCanvas(targetWidth, targetHeight)
    const ctx = canvas.getContext('2d', { willReadFrequently: anyFilter })
    const videoStream = canvas.captureStream(fps)
    const mimeType = pickMimeType()
    post({ type: 'mimeType', mimeType })

    const chunks = []
    let recorder = null
    const makeRecorder = () => {
      recorder = new MediaRecorder(videoStream, {
        mimeType,
        videoBitsPerSecond: targetBitrate,
      })
      recorder.ondataavailable = (ev) => {
        if (ev.data && ev.data.size > 0) chunks.push(ev.data)
      }
    }
    makeRecorder()

    // 解码器
    let decoder = null
    const initDecoder = () => {
      decoder = new VideoDecoder({
        output: (frame) => {
          ctx.drawImage(frame, 0, 0, targetWidth, targetHeight)
          if (anyFilter) {
            applyFilterChainInPlace(ctx, targetWidth, targetHeight, filters)
          }
          frame.close()
        },
        error: (err) =>
          post({ type: 'error', message: '解码失败: ' + err.message }),
      })
      decoder.configure({
        codec: codecToWc(info.codec),
        codedWidth: info.width,
        codedHeight: info.height,
      })
    }
    initDecoder()

    const startTime = performance.now()
    const totalSamples = info.sampleCount
    let processedSamples = 0

    let segIdx = 0
    let segStart = 0
    while (segStart < info.totalDuration) {
      const segEnd = Math.min(info.totalDuration, segStart + chunkDuration)
      const { start, end } = findSampleRange(info, segStart, segEnd)

      post({ type: 'segment', index: segIdx, start: segStart, end: segEnd, chunkDuration })

      recorder.start(1000)
      await new Promise((r) => setTimeout(r, 30))

      for (let i = start; i <= end; i++) {
        const off = info.sampleOffsets[i]
        const size = info.sampleSizes[i]
        const buf = await file.slice(off, off + size).arrayBuffer()
        const timestamp = ((info.dts[i] + info.cts[i]) * 1000000) / info.timescale
        const duration =
          i + 1 < totalSamples
            ? ((info.dts[i + 1] - info.dts[i]) * 1000000) / info.timescale
            : 0
        const isKey = info.syncSamples === null || info.syncSamples.has(i + 1)
        const chunk = new EncodedVideoChunk({
          type: isKey ? 'key' : 'delta',
          timestamp,
          duration,
          data: buf,
        })
        decoder.decode(chunk)
        processedSamples++
      }
      await decoder.flush()

      await new Promise((resolve) => {
        recorder.onstop = resolve
        recorder.stop()
      })

      const mem = getMemory()
      post({ type: 'memory', memory: mem })
      if (mem.used > 1024 * 1024 * 1024 && chunkDuration > MIN_CHUNK) {
        chunkDuration = Math.max(MIN_CHUNK, Math.floor(chunkDuration / 2))
        post({ type: 'degrade', chunkDuration })
      }

      segStart = segEnd
      segIdx++

      const progress = totalSamples > 0 ? processedSamples / totalSamples : 0
      const elapsed = (performance.now() - startTime) / 1000
      const eta = progress > 0 ? (elapsed / progress) * (1 - progress) : 0
      post({
        type: 'progress',
        progress,
        elapsed,
        eta,
        processedSamples,
        totalSamples,
      })
    }

    decoder.close()

    const blob = new Blob(chunks, { type: mimeType.split(';')[0] })
    post({ type: 'done', blob, size: blob.size }, [blob])
  } catch (err) {
    post({ type: 'error', message: err.message || String(err) })
  }
}

// ===== 滤镜实现（内联到 Worker，避免 import 问题）=====

function applyFilterChainInPlace(ctx, width, height, filters) {
  const img = ctx.getImageData(0, 0, width, height)
  const d = img.data
  for (const f of filters) {
    if (!f.enabled) continue
    switch (f.type) {
      case 'grayscale':
        applyGrayscale(d)
        break
      case 'brightness_contrast':
        applyBrightnessContrast(d, f.brightness || 0, f.contrast || 0)
        break
      case 'blur':
        applyGaussianBlur(d, width, height, f.radius || 3)
        break
      case 'edge':
        applyEdgeDetect(d, width, height, f.threshold || 50)
        break
    }
  }
  ctx.putImageData(img, 0, 0)
}

function applyGrayscale(d) {
  for (let i = 0; i < d.length; i += 4) {
    const l = (d[i] * 299 + d[i + 1] * 587 + d[i + 2] * 114) / 1000
    d[i] = d[i + 1] = d[i + 2] = l
  }
}

function applyBrightnessContrast(d, brightness, contrast) {
  const b = Math.round(brightness * 2.55)
  const c = contrast
  const factor = (259 * (c + 255)) / (255 * (259 - c))
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
}

function applyGaussianBlur(data, width, height, radius) {
  radius = Math.max(1, Math.min(5, Math.round(radius)))
  const size = radius * 2 + 1
  const kernel = makeGaussianKernel(size)
  const tmp = new Uint8ClampedArray(data.length)
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let r = 0, g = 0, b = 0, a = 0
      for (let k = 0; k < size; k++) {
        const px = x - radius + k
        const cx = px < 0 ? 0 : px >= width ? width - 1 : px
        const idx = (y * width + cx) * 4
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
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let r = 0, g = 0, b = 0, a = 0
      for (let k = 0; k < size; k++) {
        const py = y - radius + k
        const cy = py < 0 ? 0 : py >= height ? height - 1 : py
        const idx = (cy * width + x) * 4
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

function applyEdgeDetect(data, width, height, threshold) {
  const gray = new Uint8ClampedArray(width * height)
  for (let i = 0, j = 0; i < data.length; i += 4, j++) {
    gray[j] = (data[i] * 299 + data[i + 1] * 587 + data[i + 2] * 114) / 1000
  }
  const t2 = Math.max(0, Math.min(255, threshold))
  for (let y = 0; y < height; y++) {
    const ym = y === 0 ? 0 : y - 1
    const yp = y === height - 1 ? y : y + 1
    for (let x = 0; x < width; x++) {
      const xm = x === 0 ? 0 : x - 1
      const xp = x === width - 1 ? x : x + 1
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
      const v = mag > t2 ? 255 : 0
      const o = (y * width + x) * 4
      data[o] = v
      data[o + 1] = v
      data[o + 2] = v
    }
  }
}

// ===== 工具函数 =====

function post(msg, transfer) {
  if (transfer) self.postMessage(msg, transfer)
  else self.postMessage(msg)
}

function codecToWc(codec) {
  if (codec === 'avc1' || codec === 'avcC') return 'avc1.42E01E'
  if (codec === 'hvc1' || codec === 'hev1') return 'hvc1.1.6.L93.B0'
  if (codec === 'vp09') return 'vp09.00.10.08'
  if (codec === 'av01') return 'av01.0.08M.08'
  return codec
}

function pickMimeType() {
  const candidates = [
    'video/webm;codecs=av1',
    'video/webm;codecs=vp9',
    'video/webm;codecs=vp8',
    'video/webm',
  ]
  for (const t of candidates) {
    if (typeof MediaRecorder !== 'undefined' && MediaRecorder.isTypeSupported(t)) return t
  }
  return 'video/webm'
}

function estimateFps(info) {
  if (info.sampleCount < 2) return 30
  const avgDelta = info.dts[info.sampleCount - 1] / (info.sampleCount - 1)
  return Math.round(info.timescale / avgDelta) || 30
}

function estimateBitrate(file, info) {
  if (!info.totalDuration) return 2_000_000
  return Math.round((file.size * 8) / info.totalDuration)
}

function getMemory() {
  if (performance.memory) {
    return {
      used: performance.memory.usedJSHeapSize,
      total: performance.memory.totalJSHeapSize,
      limit: performance.memory.jsHeapSizeLimit,
      source: 'performance.memory',
    }
  }
  return { used: 0, total: 0, limit: 0, source: 'unavailable' }
}
