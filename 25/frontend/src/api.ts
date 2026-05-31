export interface OpLog {
  id: number
  file_a: string
  file_b: string
  operation: string
  grid_size: number
  status: string
  result_file: string | null
  created_at: string
  duration_ms: number | null
  error_message: string | null
}

export async function uploadFile(file: File): Promise<{ filename: string; url: string }> {
  const fd = new FormData()
  fd.append('file', file)
  const res = await fetch('/api/upload', { method: 'POST', body: fd })
  if (!res.ok) throw new Error('上传失败: ' + res.statusText)
  return res.json()
}

export async function createLog(payload: {
  file_a: string
  file_b: string
  operation: string
  grid_size: number
  status: string
  result_file?: string | null
  duration_ms?: number | null
  error_message?: string | null
}): Promise<OpLog> {
  const res = await fetch('/api/logs', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  })
  if (!res.ok) throw new Error('写入日志失败: ' + res.statusText)
  return res.json()
}

export async function fetchLogs(): Promise<OpLog[]> {
  const res = await fetch('/api/logs')
  if (!res.ok) throw new Error('获取日志失败')
  return res.json()
}
