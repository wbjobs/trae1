import init, { boolean_operation, get_progress, get_message, init_hook, compute_volume } from '../../public/wasm/stl_bool.js'

let wasmReady = false

export async function ensureWasm(): Promise<void> {
  if (wasmReady) return
  await init()
  init_hook()
  wasmReady = true
}

export interface BoolOptions {
  op: 'union' | 'intersect' | 'difference'
  gridSize?: number
  onProgress?: (progress: number, message: string) => void
}

export async function runBoolean(
  a: ArrayBuffer,
  b: ArrayBuffer,
  opts: BoolOptions
): Promise<ArrayBuffer> {
  await ensureWasm()
  const gridSize = opts.gridSize ?? 80
  const op = opts.op

  let cancelled = false
  const poll = window.setInterval(() => {
    if (cancelled) return
    const p = get_progress()
    const m = get_message()
    opts.onProgress?.(p, m)
  }, 120)

  try {
    const aBytes = new Uint8Array(a)
    const bBytes = new Uint8Array(b)
    const result = await boolean_operation(aBytes, bBytes, op, gridSize)
    opts.onProgress?.(1.0, '完成')
    return result.buffer.slice(result.byteOffset, result.byteOffset + result.byteLength)
  } finally {
    cancelled = true
    clearInterval(poll)
  }
}

export async function getVolume(stlBuffer: ArrayBuffer): Promise<number> {
  await ensureWasm()
  const bytes = new Uint8Array(stlBuffer)
  return compute_volume(bytes)
}
