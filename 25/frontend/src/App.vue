<template>
  <div class="app">
    <aside class="sidebar">
      <h2>STL 布尔运算</h2>

      <div class="section">
        <div class="file-row">
          <label>模型 A</label>
          <input type="file" accept=".stl" @change="onFileA" />
          <div class="msg">{{ fileA?.name ?? '未选择' }}</div>
        </div>
        <div class="file-row" style="margin-top:10px;">
          <label>模型 B</label>
          <input type="file" accept=".stl" @change="onFileB" />
          <div class="msg">{{ fileB?.name ?? '未选择' }}</div>
        </div>
      </div>

      <div class="section">
        <div class="row">
          <label style="font-size:13px;color:#bbb;">运算:</label>
          <select v-model="op">
            <option value="union">并集 (Union)</option>
            <option value="intersect">交集 (Intersection)</option>
            <option value="difference">差集 A-B (Difference)</option>
          </select>
        </div>
        <div class="row" style="margin-top:8px;">
          <label style="font-size:13px;color:#bbb;">分辨率:</label>
          <input type="number" v-model.number="gridSize" min="20" max="200" step="5" style="width:80px;" />
          <span class="msg">体素网格数</span>
        </div>
        <div class="row" style="margin-top:8px;">
          <label style="font-size:13px;color:#bbb;">预览模式:</label>
          <select v-model="showInputs">
            <option value="inputs">显示输入</option>
            <option value="result">只显示结果</option>
          </select>
        </div>
        <button style="margin-top:12px;width:100%;" :disabled="running || !fileA || !fileB" @click="run">
          {{ running ? '运算中...' : '执行布尔运算' }}
        </button>
      </div>

      <div class="section">
        <div class="progress-wrap"><div class="progress-bar" :style="{ width: (progress*100).toFixed(1) + '%' }"></div></div>
        <div class="msg">{{ message || '等待执行' }}</div>
      </div>

      <div class="section">
        <div style="font-size:13px;color:#ffb74d;font-weight:600;margin-bottom:6px;">体积测量 (mm³)</div>
        <div class="vol-grid">
          <div class="vol-label">模型 A</div>
          <div class="vol-value">{{ fmtVol(volumeA) }}</div>
          <div class="vol-label">模型 B</div>
          <div class="vol-value">{{ fmtVol(volumeB) }}</div>
          <div class="vol-label vol-result-label">运算结果</div>
          <div class="vol-value vol-result-value">{{ fmtVol(volumeResult) }}</div>
        </div>
        <button
          style="margin-top:10px;width:100%;padding:8px;font-size:13px;background:#4fc3f7;color:#111;"
          :disabled="!hasVolumeData"
          @click="exportCSV"
        >
          导出体积报告 (CSV)
        </button>
      </div>

      <div class="section">
        <div style="font-size:13px;color:#bbb;margin-bottom:4px;">运算日志</div>
        <button style="width:100%;padding:6px;font-size:12px;" @click="refreshLogs">刷新</button>
        <div class="logs">
          <table>
            <thead>
              <tr><th>时间</th><th>操作</th><th>状态</th></tr>
            </thead>
            <tbody>
              <tr v-for="l in logs" :key="l.id">
                <td>{{ formatTime(l.created_at) }}</td>
                <td>{{ l.operation }}<br/><span style="color:#777">{{ l.file_a }} - {{ l.file_b }}</span></td>
                <td>{{ l.status }}</td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>
    </aside>
    <div class="viewer" ref="viewerRef"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { Viewer3D } from './viewer'
import { runBoolean, getVolume, ensureWasm } from './wasm/stlb'
import { uploadFile, createLog, fetchLogs, type OpLog } from './api'

const viewerRef = ref<HTMLElement | null>(null)
let viewer: Viewer3D | null = null

const fileA = ref<File | null>(null)
const fileB = ref<File | null>(null)
const op = ref<'union' | 'intersect' | 'difference'>('difference')
const gridSize = ref<number>(80)
const showInputs = ref<'inputs' | 'result'>('inputs')
const running = ref(false)
const progress = ref(0)
const message = ref('')
const logs = ref<OpLog[]>([])

const volumeA = ref<number | null>(null)
const volumeB = ref<number | null>(null)
const volumeResult = ref<number | null>(null)

interface VolumeRecord {
  timestamp: string
  file_a: string
  file_b: string
  operation: string
  volume_a_mm3: string
  volume_b_mm3: string
  volume_result_mm3: string
  duration_ms: number
  grid_size: number
}
const volumeHistory = ref<VolumeRecord[]>([])

const hasVolumeData = computed(() => volumeHistory.value.length > 0)

let aBuf: ArrayBuffer | null = null
let bBuf: ArrayBuffer | null = null

onMounted(async () => {
  if (viewerRef.value) {
    viewer = new Viewer3D(viewerRef.value)
  }
  try {
    await ensureWasm()
  } catch (e) {
    console.error('WASM init failed:', e)
  }
  refreshLogs()
})

onUnmounted(() => {
  viewer?.dispose()
})

function fmtVol(v: number | null): string {
  if (v === null) return '—'
  return v.toFixed(2) + ' mm³'
}

async function onFileA(e: Event) {
  const target = e.target as HTMLInputElement
  const f = target.files?.[0]
  if (!f) return
  fileA.value = f
  aBuf = await f.arrayBuffer()
  volumeA.value = null
  getVolume(aBuf).then(v => { volumeA.value = v }).catch(() => {})
  rerender()
}

async function onFileB(e: Event) {
  const target = e.target as HTMLInputElement
  const f = target.files?.[0]
  if (!f) return
  fileB.value = f
  bBuf = await f.arrayBuffer()
  volumeB.value = null
  getVolume(bBuf).then(v => { volumeB.value = v }).catch(() => {})
  rerender()
}

function rerender() {
  viewer?.clear()
  if (showInputs.value === 'inputs') {
    if (aBuf) viewer?.loadSTL(aBuf, 0xffb74d, 0.55)
    if (bBuf) viewer?.loadSTL(bBuf, 0x4fc3f7, 0.55)
  }
}

watch(showInputs, rerender)

async function refreshLogs() {
  try {
    logs.value = await fetchLogs()
  } catch (e) {
    console.error(e)
  }
}

function formatTime(iso: string) {
  const d = new Date(iso)
  return d.toLocaleTimeString()
}

const OP_NAMES: Record<string, string> = {
  union: '并集',
  intersect: '交集',
  difference: '差集A-B'
}

async function run() {
  if (!fileA.value || !fileB.value || !aBuf || !bBuf) return
  running.value = true
  progress.value = 0
  message.value = '上传文件...'
  volumeResult.value = null

  const t0 = performance.now()
  let uploadedA: { filename: string; url: string } | null = null
  let uploadedB: { filename: string; url: string } | null = null

  try {
    uploadedA = await uploadFile(fileA.value)
    uploadedB = await uploadFile(fileB.value)

    if (volumeA.value === null) {
      volumeA.value = await getVolume(aBuf)
    }
    if (volumeB.value === null) {
      volumeB.value = await getVolume(bBuf)
    }

    message.value = 'WASM 运算中...'
    const result = await runBoolean(aBuf, bBuf, {
      op: op.value,
      gridSize: gridSize.value,
      onProgress: (p, m) => {
        progress.value = p
        message.value = m
      }
    })

    viewer?.clear()
    viewer?.loadSTL(result, 0x81c784, 1.0)

    message.value = '计算体积...'
    volumeResult.value = await getVolume(result)

    const dur = performance.now() - t0
    const rec: VolumeRecord = {
      timestamp: new Date().toISOString(),
      file_a: uploadedA.filename,
      file_b: uploadedB.filename,
      operation: OP_NAMES[op.value] ?? op.value,
      volume_a_mm3: volumeA.value.toFixed(2),
      volume_b_mm3: volumeB.value.toFixed(2),
      volume_result_mm3: volumeResult.value.toFixed(2),
      duration_ms: Math.round(dur),
      grid_size: gridSize.value
    }
    volumeHistory.value.push(rec)

    await createLog({
      file_a: uploadedA.filename,
      file_b: uploadedB.filename,
      operation: op.value,
      grid_size: gridSize.value,
      status: 'success',
      duration_ms: Math.round(dur)
    })
    message.value = `完成，耗时 ${Math.round(dur)}ms`
  } catch (err: any) {
    console.error(err)
    message.value = '错误: ' + (err?.message ?? String(err))
    try {
      await createLog({
        file_a: uploadedA?.filename ?? fileA.value.name,
        file_b: uploadedB?.filename ?? fileB.value.name,
        operation: op.value,
        grid_size: gridSize.value,
        status: 'failed',
        error_message: err?.message ?? String(err)
      })
    } catch {}
  } finally {
    running.value = false
    progress.value = 1
    refreshLogs()
  }
}

function exportCSV() {
  if (volumeHistory.value.length === 0) return
  const header = [
    '时间',
    '模型A',
    '模型B',
    '运算类型',
    '模型A体积(mm³)',
    '模型B体积(mm³)',
    '结果体积(mm³)',
    '耗时(ms)',
    '网格分辨率'
  ]
  const rows = volumeHistory.value.map(r => [
    r.timestamp,
    r.file_a,
    r.file_b,
    r.operation,
    r.volume_a_mm3,
    r.volume_b_mm3,
    r.volume_result_mm3,
    String(r.duration_ms),
    String(r.grid_size)
  ])
  const escape = (s: string) => {
    if (s.includes(',') || s.includes('"') || s.includes('\n')) {
      return '"' + s.replace(/"/g, '""') + '"'
    }
    return s
  }
  const csv = [header, ...rows]
    .map(row => row.map(escape).join(','))
    .join('\n')
  const blob = new Blob(['\ufeff' + csv], { type: 'text/csv;charset=utf-8;' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `volume_report_${Date.now()}.csv`
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  URL.revokeObjectURL(url)
}
</script>
