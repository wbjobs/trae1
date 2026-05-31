import dayjs from 'dayjs'

export function formatDate(date: string | Date | number, fmt = 'YYYY-MM-DD HH:mm:ss') {
  if (!date) return '-'
  return dayjs(date).format(fmt)
}

export function methodColor(method: string) {
  const map: Record<string, string> = {
    GET: 'tag-get',
    POST: 'tag-post',
    PUT: 'tag-put',
    DELETE: 'tag-delete',
    PATCH: 'tag-patch'
  }
  return map[method?.toUpperCase()] || 'tag-get'
}

export function genId(prefix = 'id') {
  return `${prefix}_${Date.now()}_${Math.floor(Math.random() * 10000)}`
}

export function copyToClipboard(text: string) {
  if (navigator.clipboard) {
    navigator.clipboard.writeText(text)
  } else {
    const ta = document.createElement('textarea')
    ta.value = text
    document.body.appendChild(ta)
    ta.select()
    document.execCommand('copy')
    document.body.removeChild(ta)
  }
}
