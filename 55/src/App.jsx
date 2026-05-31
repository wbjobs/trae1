import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import TranscodeWorker from './transcode.worker.js?worker'
import {
  FILTER_TYPES,
  createDefaultFilters,
  applyFilterChain,
} from './lib/filters.js'
import './App.css'

const MAX_QUEUE = 3

function formatBytes(n) {
  if (n == null || isNaN(n)) return '-'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let i = 0
  while (n >= 1024 && i < units.length - 1) {
    n /= 1024
    i++
  }
  return n.toFixed(n < 10 && i > 0 ? 2 : 1) + ' ' + units[i]
}

function formatTime(s) {
  if (!isFinite(s) || s < 0) return '-'
  s = Math.round(s)
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  const sec = s % 60
  return (h > 0 ? h + 'h ' : '') + (m > 0 ? m + 'm ' : '') + sec + 's'
}

export default function App() {
  const [jobs, setJobs] = useState([])
  const [memory, setMemory] = useState({ used: 0, limit: 0, source: 'unavailable' })
  const [dragging, setDragging] = useState(false)
  const [filters, setFilters] = useState(createDefaultFilters())
  const [previewFile, setPreviewFile] = useState(null)
  const [previewUrl, setPreviewUrl] = useState(null)
  const fileInputRef = useRef(null)
  const workersRef = useRef(new Map())

  const previewVideoRef = useRef(null)
  const previewCanvasRef = useRef(null)
  const previewRafRef = useRef(0)

  // 内存监测
  useEffect(() => {
    const id = setInterval(() => {
      if (performance.memory) {
        setMemory({
          used: performance.memory.usedJSHeapSize,
          limit: performance.memory.jsHeapSizeLimit,
          source: 'performance.memory',
        })
      }
    }, 1000)
    return () => clearInterval(id)
  }, [])

  // 滤镜预览循环
  useEffect(() => {
    const video = previewVideoRef.current
    const canvas = previewCanvasRef.current
    if (!video || !canvas) return
    const ctx = canvas.getContext('2d', { willReadFrequently: true })
    let stopped = false

    const loop = () => {
      if (stopped) return
      if (video.readyState >= 2 && video.videoWidth > 0) {
        const w = video.videoWidth
        const h = video.videoHeight
        if (canvas.width !== w) canvas.width = w
        if (canvas.height !== h) canvas.height = h
        ctx.drawImage(video, 0, 0, w, h)
        const anyEnabled = filters.some((f) => f.enabled)
        if (anyEnabled) {
          const img = ctx.getImageData(0, 0, w, h)
          applyFilterChain(filters, img.data, w, h)
          ctx.putImageData(img, 0, 0)
        }
      }
      previewRafRef.current = requestAnimationFrame(loop)
    }
    previewRafRef.current = requestAnimationFrame(loop)
    return () => {
      stopped = true
      cancelAnimationFrame(previewRafRef.current)
    }
  }, [filters, previewUrl])

  useEffect(() => {
    return () => {
      if (previewUrl) URL.revokeObjectURL(previewUrl)
    }
  }, [previewUrl])

  const addJob = useCallback(
    (file) => {
      if (jobs.length >= MAX_QUEUE) {
        alert('最多同时处理 ' + MAX_QUEUE + ' 个文件')
        return
      }
      if (!file.name.toLowerCase().endsWith('.mp4')) {
        alert('仅支持 MP4 文件')
        return
      }
      const id = crypto.randomUUID()
      setJobs((prev) => [
        ...prev,
        {
          id,
          name: file.name,
          size: file.size,
          status: 'pending',
          progress: 0,
          eta: 0,
          elapsed: 0,
          chunkDuration: 30,
          bitrate: 0,
          width: 0,
          height: 0,
          fps: 0,
          duration: 0,
          blob: null,
          url: null,
          error: null,
        },
      ])
      startJob(id, file)
      // 同时设置为预览文件
      if (!previewUrl) {
        const url = URL.createObjectURL(file)
        setPreviewUrl(url)
        setPreviewFile(file)
      }
    },
    [jobs.length, previewUrl]
  )

  const startJob = (id, file) => {
    const worker = new TranscodeWorker()
    workersRef.current.set(id, worker)

    worker.onmessage = (e) => {
      const msg = e.data
      setJobs((prev) =>
        prev.map((j) => {
          if (j.id !== id) return j
          switch (msg.type) {
            case 'meta':
              return { ...j, status: 'running' }
            case 'params':
              return {
                ...j,
                width: msg.width,
                height: msg.height,
                bitrate: msg.bitrate,
                fps: msg.fps,
                duration: msg.duration,
              }
            case 'segment':
              return { ...j, chunkDuration: msg.chunkDuration }
            case 'progress':
              return {
                ...j,
                progress: msg.progress,
                elapsed: msg.elapsed,
                eta: msg.eta,
              }
            case 'degrade':
              return { ...j, chunkDuration: msg.chunkDuration, degraded: true }
            case 'done':
              const url = URL.createObjectURL(msg.blob)
              if (j.url) URL.revokeObjectURL(j.url)
              worker.terminate()
              workersRef.current.delete(id)
              return {
                ...j,
                status: 'done',
                progress: 1,
                blob: msg.blob,
                url,
                size: msg.size,
              }
            case 'error':
              worker.terminate()
              workersRef.current.delete(id)
              return { ...j, status: 'error', error: msg.message }
            default:
              return j
          }
        })
      )
    }

    worker.postMessage({
      type: 'start',
      file,
      options: { filters },
    })
  }

  const removeJob = (id) => {
    setJobs((prev) => {
      const w = workersRef.current.get(id)
      if (w) {
        w.terminate()
        workersRef.current.delete(id)
      }
      const j = prev.find((x) => x.id === id)
      if (j?.url) URL.revokeObjectURL(j.url)
      return prev.filter((x) => x.id !== id)
    })
  }

  const downloadJob = (job) => {
    if (!job.blob) return
    const a = document.createElement('a')
    a.href = job.url
    a.download = job.name.replace(/\.mp4$/i, '') + '_av1.webm'
    a.click()
  }

  const handleFiles = (files) => {
    for (const f of files) addJob(f)
  }

  const onDrop = (e) => {
    e.preventDefault()
    setDragging(false)
    handleFiles(e.dataTransfer.files)
  }

  const memoryPercent = useMemo(() => {
    if (!memory.limit) return 0
    return Math.min(100, (memory.used / memory.limit) * 100)
  }, [memory])

  const toggleFilter = (type) => {
    setFilters((prev) =>
      prev.map((f) => (f.type === type ? { ...f, enabled: !f.enabled } : f))
    )
  }

  const updateFilter = (type, patch) => {
    setFilters((prev) =>
      prev.map((f) => (f.type === type ? { ...f, ...patch } : f))
    )
  }

  return (
    <div className="App">
      <h1>浏览器端视频转码工具</h1>
      <p className="subtitle">
        H.264 MP4 → AV1 WebM · 1080p → 720p · 码率减半 · 滤镜 · 纯浏览器端
      </p>

      {/* 滤镜面板 */}
      <div className="filter-panel">
        <div className="filter-panel-title">
          <strong>视频滤镜</strong>
          <span className="hint">可叠加多个滤镜，按启用顺序应用</span>
        </div>
        <div className="filter-grid">
          <FilterCard
            label="灰度化"
            enabled={filters.find((f) => f.type === FILTER_TYPES.GRAYSCALE).enabled}
            onToggle={() => toggleFilter(FILTER_TYPES.GRAYSCALE)}
          />
          <FilterCard
            label="亮度 / 对比度"
            enabled={
              filters.find((f) => f.type === FILTER_TYPES.BRIGHTNESS_CONTRAST).enabled
            }
            onToggle={() => toggleFilter(FILTER_TYPES.BRIGHTNESS_CONTRAST)}
          >
            <FilterSlider
              label="亮度"
              min={-100}
              max={100}
              value={filters.find((f) => f.type === FILTER_TYPES.BRIGHTNESS_CONTRAST).brightness}
              onChange={(v) =>
                updateFilter(FILTER_TYPES.BRIGHTNESS_CONTRAST, { brightness: v })
              }
            />
            <FilterSlider
              label="对比度"
              min={-100}
              max={100}
              value={filters.find((f) => f.type === FILTER_TYPES.BRIGHTNESS_CONTRAST).contrast}
              onChange={(v) =>
                updateFilter(FILTER_TYPES.BRIGHTNESS_CONTRAST, { contrast: v })
              }
            />
          </FilterCard>
          <FilterCard
            label="高斯模糊"
            enabled={filters.find((f) => f.type === FILTER_TYPES.BLUR).enabled}
            onToggle={() => toggleFilter(FILTER_TYPES.BLUR)}
          >
            <FilterSlider
              label="半径"
              min={1}
              max={5}
              step={1}
              value={filters.find((f) => f.type === FILTER_TYPES.BLUR).radius}
              onChange={(v) => updateFilter(FILTER_TYPES.BLUR, { radius: v })}
            />
          </FilterCard>
          <FilterCard
            label="边缘检测"
            enabled={filters.find((f) => f.type === FILTER_TYPES.EDGE).enabled}
            onToggle={() => toggleFilter(FILTER_TYPES.EDGE)}
          >
            <FilterSlider
              label="阈值"
              min={0}
              max={200}
              value={filters.find((f) => f.type === FILTER_TYPES.EDGE).threshold}
              onChange={(v) => updateFilter(FILTER_TYPES.EDGE, { threshold: v })}
            />
          </FilterCard>
        </div>
      </div>

      {/* 实时预览 */}
      <div className="preview-panel">
        <div className="filter-panel-title">
          <strong>实时预览</strong>
          <span className="hint">上传文件后自动显示滤镜效果</span>
        </div>
        <div className="preview-row">
          <div className="preview-slot">
            <div className="preview-label">原始</div>
            <video
              ref={previewVideoRef}
              src={previewUrl}
              muted
              loop
              playsInline
            />
          </div>
          <div className="preview-slot">
            <div className="preview-label">滤镜后</div>
            <canvas ref={previewCanvasRef} />
          </div>
        </div>
      </div>

      {/* 上传区 */}
      <div
        className={'drop-zone ' + (dragging ? 'dragging' : '')}
        onClick={() => fileInputRef.current?.click()}
        onDragOver={(e) => {
          e.preventDefault()
          setDragging(true)
        }}
        onDragLeave={() => setDragging(false)}
        onDrop={onDrop}
      >
        点击或拖拽 MP4 文件到此处上传（最多 {MAX_QUEUE} 个）
        <input
          ref={fileInputRef}
          type="file"
          accept="video/mp4,.mp4"
          multiple
          onChange={(e) => handleFiles(e.target.files)}
        />
      </div>

      {/* 内存 */}
      <div className="memory-panel">
        <span>内存占用</span>
        <div className="memory-bar">
          <div style={{ width: memoryPercent + '%' }} />
        </div>
        <span className="value">
          {formatBytes(memory.used)} / {memory.limit ? formatBytes(memory.limit) : '未知'}
          {memoryPercent > 85 && <span style={{ color: '#f14668' }}> · 高负载</span>}
        </span>
      </div>

      {/* 队列 */}
      <div className="queue">
        {jobs.length === 0 && (
          <p className="hint" style={{ textAlign: 'center', marginTop: 24 }}>
            转码队列为空。请上传 MP4 文件开始转码。
          </p>
        )}
        {jobs.map((job) => (
          <div className="job-card" key={job.id}>
            <div className="row">
              <div className="name">{job.name}</div>
              <span className={'status ' + job.status}>
                {job.status === 'pending'
                  ? '等待中'
                  : job.status === 'running'
                  ? '转码中'
                  : job.status === 'done'
                  ? '已完成'
                  : '失败'}
              </span>
            </div>
            <div className="meta">
              原始大小：{formatBytes(job.size)}
              {job.width > 0 && (
                <>
                  {' · '}输出：{job.width}×{job.height} · {formatBytes(job.bitrate / 8)}/s ·{' '}
                  {job.fps}fps · 时长 {formatTime(job.duration)}
                </>
              )}
              {job.degraded && (
                <span style={{ color: '#f5b642' }}>
                  {' '}
                  · 内存降级 分片 {job.chunkDuration}s
                </span>
              )}
            </div>
            {job.status === 'running' && (
              <>
                <div className="progress">
                  <div style={{ width: Math.round(job.progress * 100) + '%' }} />
                </div>
                <div className="meta">
                  进度 {Math.round(job.progress * 100)}% · 已用时 {formatTime(job.elapsed)} ·
                  预计剩余 {formatTime(job.eta)}
                </div>
              </>
            )}
            {job.status === 'done' && (
              <>
                <div className="meta">输出大小：{formatBytes(job.size)}</div>
                <div className="preview">
                  <video src={job.url} controls />
                </div>
              </>
            )}
            {job.status === 'error' && (
              <div className="meta" style={{ color: '#f14668' }}>
                错误：{job.error}
              </div>
            )}
            <div className="buttons">
              {job.status === 'done' && (
                <button className="btn primary" onClick={() => downloadJob(job)}>
                  下载
                </button>
              )}
              <button
                className="btn"
                onClick={() => removeJob(job.id)}
                disabled={job.status === 'running'}
              >
                {job.status === 'running' ? '处理中' : '移除'}
              </button>
            </div>
          </div>
        ))}
      </div>
    </div>
  )
}

function FilterCard({ label, enabled, onToggle, children }) {
  return (
    <div className={'filter-card' + (enabled ? ' active' : '')}>
      <div className="filter-card-head">
        <label className="filter-toggle">
          <input type="checkbox" checked={enabled} onChange={onToggle} />
          <span>{label}</span>
        </label>
      </div>
      {enabled && children && <div className="filter-card-body">{children}</div>}
    </div>
  )
}

function FilterSlider({ label, min, max, step = 1, value, onChange }) {
  return (
    <div className="filter-slider">
      <span>
        {label}：{value}
      </span>
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={(e) => onChange(parseFloat(e.target.value))}
      />
    </div>
  )
}
