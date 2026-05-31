// 轻量 MP4 Box 解析：仅用于读取 moov/trak/mdia/minf/stbl 下的样本信息
// 不依赖 mp4box.js 库，手写解析器以控制内存。

export function readString(buf, offset, length) {
  let s = ''
  for (let i = 0; i < length; i++) s += String.fromCharCode(buf[offset + i])
  return s
}

export function findBox(data, offset, end, type) {
  while (offset < end) {
    const size = data.getUint32(offset)
    const t = readString(data.buffer, data.byteOffset + offset + 4, 4)
    if (t === type) return { offset, size }
    if (size < 8) break
    offset += size
  }
  return null
}

export function parseBoxes(data, offset, end) {
  const boxes = []
  while (offset + 8 <= end) {
    const size = data.getUint32(offset)
    const type = readString(data.buffer, data.byteOffset + offset + 4, 4)
    if (size < 8) break
    boxes.push({ offset, size, type })
    offset += size
  }
  return boxes
}

// 从 MP4 文件解析出视频轨道的样本表（时间戳、大小、偏移、关键帧标志）
export async function parseMp4Samples(file) {
  // 先读头部，找到 moov box 位置
  const head = new DataView(await file.slice(0, Math.min(file.size, 1024 * 1024)).arrayBuffer())
  const boxes = parseBoxes(head, 0, head.byteLength)
  let moovOffset = -1
  let moovSize = 0
  for (const b of boxes) {
    if (b.type === 'moov') {
      moovOffset = b.offset
      moovSize = b.size
      break
    }
  }
  if (moovOffset < 0) {
    // moov 可能在文件末尾（mdat 在前面），需要扫描最后一段
    const tail = new DataView(
      await file.slice(Math.max(0, file.size - 8 * 1024 * 1024)).arrayBuffer()
    )
    const base = file.size - tail.byteLength
    const tailBoxes = parseBoxes(tail, 0, tail.byteLength)
    for (const b of tailBoxes) {
      if (b.type === 'moov') {
        moovOffset = base + b.offset
        moovSize = b.size
        break
      }
    }
  }
  if (moovOffset < 0) throw new Error('未找到 moov box，不是有效的 MP4 文件')

  const moovData = new DataView(await file.slice(moovOffset, moovOffset + moovSize).arrayBuffer())

  // 遍历 traks，找到视频轨道
  const traks = []
  let p = 8
  while (p < moovSize) {
    const size = moovData.getUint32(p)
    const type = readString(moovData.buffer, moovData.byteOffset + p + 4, 4)
    if (type === 'trak') traks.push({ offset: p, size })
    if (size < 8) break
    p += size
  }

  for (const trak of traks) {
    const info = parseTrak(moovData, trak.offset, trak.size, moovOffset)
    if (info && info.kind === 'video') {
      info.mdatOffset = findMdatOffset(file, moovData, moovOffset)
      return info
    }
  }
  throw new Error('未找到视频轨道')
}

function findMdatOffset(file) {
  // mdat 通常在文件开头，读取前 1MB 查找
  // 实际上我们用 stbl 的 chunk offset（相对文件起始），所以 mdat 的具体位置不重要
  // 但需要返回文件起始偏移（通常为 0）
  return 0
}

function parseTrak(moovData, offset, size, moovBase) {
  // 找 mdia -> minf -> stbl -> stsd/stts/ctts/stss/stsc/stsz/co64/co64
  let mdia = null,
    minf = null,
    stbl = null,
    tkhd = null
  let p = offset + 8
  const end = offset + size
  while (p < end) {
    const s = moovData.getUint32(p)
    const t = readString(moovData.buffer, moovData.byteOffset + p + 4, 4)
    if (t === 'tkhd') tkhd = { offset: p, size: s }
    if (t === 'mdia') mdia = { offset: p, size: s }
    if (s < 8) break
    p += s
  }
  if (!mdia || !tkhd) return null

  p = mdia.offset + 8
  const mdiaEnd = mdia.offset + mdia.size
  while (p < mdiaEnd) {
    const s = moovData.getUint32(p)
    const t = readString(moovData.buffer, moovData.byteOffset + p + 4, 4)
    if (t === 'minf') minf = { offset: p, size: s }
    if (s < 8) break
    p += s
  }
  if (!minf) return null

  p = minf.offset + 8
  const minfEnd = minf.offset + minf.size
  while (p < minfEnd) {
    const s = moovData.getUint32(p)
    const t = readString(moovData.buffer, moovData.byteOffset + p + 4, 4)
    if (t === 'stbl') stbl = { offset: p, size: s }
    if (s < 8) break
    p += s
  }
  if (!stbl) return null

  // 解析 tkhd：宽高
  const tkhdVer = moovData.getUint8(tkhd.offset + 8)
  const width = moovData.getUint32(tkhd.offset + (tkhdVer === 1 ? 88 : 84)) >> 16
  const height = moovData.getUint32(tkhd.offset + (tkhdVer === 1 ? 92 : 88)) >> 16

  // 解析 stbl 下的子 box
  const stblEnd = stbl.offset + stbl.size
  p = stbl.offset + 8
  const stblChildren = {}
  while (p < stblEnd) {
    const s = moovData.getUint32(p)
    const t = readString(moovData.buffer, moovData.byteOffset + p + 4, 4)
    stblChildren[t] = { offset: p, size: s }
    if (s < 8) break
    p += s
  }

  // stsd 判读编解码器
  if (!stblChildren.stsd) return null
  const stsd = stblChildren.stsd
  const sampleDescCount = moovData.getUint32(stsd.offset + 12)
  if (sampleDescCount < 1) return null
  const firstEntry = stsd.offset + 16
  const codec = readString(moovData.buffer, moovData.byteOffset + firstEntry + 4, 4)
  const isVideo = ['avc1', 'avcC', 'hvc1', 'hev1', 'vp08', 'vp09', 'av01', 'mp4v'].includes(codec)
  if (!isVideo) return { kind: 'audio' }

  // 读取 stts（解码时间戳）
  let timeToSamples = []
  if (stblChildren.stts) {
    const s = stblChildren.stts
    const count = moovData.getUint32(s.offset + 12)
    for (let i = 0; i < count; i++) {
      const o = s.offset + 16 + i * 8
      timeToSamples.push({
        count: moovData.getUint32(o),
        delta: moovData.getUint32(o + 4),
      })
    }
  }

  // ctts（合成时间偏移，B 帧用）
  let compTimeToSamples = []
  if (stblChildren.ctts) {
    const s = stblChildren.ctts
    const ver = moovData.getUint8(s.offset + 8)
    const count = moovData.getUint32(s.offset + 12)
    for (let i = 0; i < count; i++) {
      const o = s.offset + 16 + i * 12
      compTimeToSamples.push({
        count: moovData.getUint32(o),
        offset: ver === 0 ? moovData.getUint32(o + 8) : moovData.getInt32(o + 8),
      })
    }
  }

  // stss 关键帧索引（1-based）
  let syncSamples = new Set()
  if (stblChildren.stss) {
    const s = stblChildren.stss
    const count = moovData.getUint32(s.offset + 12)
    for (let i = 0; i < count; i++) {
      syncSamples.add(moovData.getUint32(s.offset + 16 + i * 4))
    }
  } else {
    // 没有 stss 表示所有样本都是关键帧
    syncSamples = null
  }

  // stsc 样本到 chunk 的映射
  let sampleToChunk = []
  if (stblChildren.stsc) {
    const s = stblChildren.stsc
    const count = moovData.getUint32(s.offset + 12)
    for (let i = 0; i < count; i++) {
      const o = s.offset + 16 + i * 12
      sampleToChunk.push({
        firstChunk: moovData.getUint32(o),
        samplesPerChunk: moovData.getUint32(o + 4),
        sampleDescIndex: moovData.getUint32(o + 8),
      })
    }
  }

  // stsz 样本大小
  let sampleSizes = []
  let totalSamples = 0
  if (stblChildren.stsz) {
    const s = stblChildren.stsz
    const sampleSize = moovData.getUint32(s.offset + 12)
    totalSamples = moovData.getUint32(s.offset + 16)
    if (sampleSize === 0) {
      for (let i = 0; i < totalSamples; i++) {
        sampleSizes.push(moovData.getUint32(s.offset + 20 + i * 4))
      }
    } else {
      sampleSizes = new Array(totalSamples).fill(sampleSize)
    }
  }

  // stco / co64 chunk 偏移
  let chunkOffsets = []
  if (stblChildren.stco) {
    const s = stblChildren.stco
    const count = moovData.getUint32(s.offset + 12)
    for (let i = 0; i < count; i++) {
      chunkOffsets.push(moovData.getUint32(s.offset + 16 + i * 4))
    }
  } else if (stblChildren.co64) {
    const s = stblChildren.co64
    const count = moovData.getUint32(s.offset + 12)
    for (let i = 0; i < count; i++) {
      // BigInt 转回 number（文件 < 8GB 没问题）
      const hi = moovData.getUint32(s.offset + 16 + i * 8)
      const lo = moovData.getUint32(s.offset + 16 + i * 8 + 4)
      chunkOffsets.push(hi * 0x100000000 + lo)
    }
  }

  // 计算每个样本的文件偏移 + 时间戳
  // 先展开 stsc 到每个 chunk 的 samplesPerChunk
  const samplesPerChunk = new Array(chunkOffsets.length).fill(0)
  for (let i = 0; i < sampleToChunk.length; i++) {
    const start = sampleToChunk[i].firstChunk - 1
    const end =
      i + 1 < sampleToChunk.length ? sampleToChunk[i + 1].firstChunk - 1 : chunkOffsets.length
    for (let c = start; c < end; c++) samplesPerChunk[c] = sampleToChunk[i].samplesPerChunk
  }

  // 计算每个样本的文件偏移
  const sampleOffsets = new Array(totalSamples)
  let sampleIdx = 0
  for (let c = 0; c < chunkOffsets.length; c++) {
    let offset = chunkOffsets[c]
    for (let k = 0; k < samplesPerChunk[c]; k++) {
      if (sampleIdx >= totalSamples) break
      sampleOffsets[sampleIdx] = offset
      offset += sampleSizes[sampleIdx] || 0
      sampleIdx++
    }
  }

  // 计算每个样本的 DTS / CTS
  const dts = new Array(totalSamples)
  let t = 0
  let idx = 0
  for (const entry of timeToSamples) {
    for (let i = 0; i < entry.count; i++) {
      dts[idx++] = t
      t += entry.delta
    }
  }

  // 展开 ctts
  const cts = new Array(totalSamples).fill(0)
  if (compTimeToSamples.length) {
    let ci = 0
    for (const entry of compTimeToSamples) {
      for (let i = 0; i < entry.count; i++) cts[ci++] = entry.offset
    }
  }

  // 读取 timescale（mdhd）
  // 回到 mdia 找 mdhd
  let mdhd = null
  p = mdia.offset + 8
  while (p < mdiaEnd) {
    const s = moovData.getUint32(p)
    const tp = readString(moovData.buffer, moovData.byteOffset + p + 4, 4)
    if (tp === 'mdhd') mdhd = { offset: p, size: s }
    if (s < 8) break
    p += s
  }
  let timescale = 30000
  if (mdhd) {
    const ver = moovData.getUint8(mdhd.offset + 8)
    timescale = moovData.getUint32(mdhd.offset + (ver === 1 ? 20 : 16))
  }

  return {
    kind: 'video',
    codec,
    width,
    height,
    timescale,
    sampleCount: totalSamples,
    sampleSizes,
    sampleOffsets,
    dts,
    cts,
    syncSamples,
    totalDuration: totalSamples > 0 ? dts[totalSamples - 1] / timescale : 0,
  }
}

// 根据时间范围（秒）找出对应的样本索引范围
export function findSampleRange(info, startTime, endTime) {
  const startTs = startTime * info.timescale
  const endTs = endTime * info.timescale
  let start = 0
  let end = info.sampleCount - 1
  // 二分查找起点：最后一个 <= startTs 的关键帧之前
  // 为了能独立解码，起点要回退到最近的关键帧
  for (let i = 0; i < info.sampleCount; i++) {
    if (info.dts[i] >= startTs) {
      start = i
      break
    }
    if (i === info.sampleCount - 1) start = i
  }
  for (let i = start; i > 0; i--) {
    if (info.syncSamples === null || info.syncSamples.has(i + 1)) {
      start = i
      break
    }
  }
  for (let i = start; i < info.sampleCount; i++) {
    if (info.dts[i] > endTs) {
      end = i - 1
      break
    }
    end = i
  }
  return { start, end }
}
