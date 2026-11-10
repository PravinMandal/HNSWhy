export function formatNumber(value: number, maximumFractionDigits = 0): string {
  return new Intl.NumberFormat('en-US', { maximumFractionDigits }).format(value)
}

export function formatCompact(value: number): string {
  return new Intl.NumberFormat('en-US', {
    notation: 'compact',
    maximumFractionDigits: value >= 1000 ? 1 : 0,
  }).format(value)
}

export function formatPercent(value: number, digits = 1): string {
  return `${(value * 100).toFixed(digits)}%`
}

export function formatMs(value: number): string {
  if (value < 1) {
    return `${value.toFixed(3)} ms`
  }
  return `${value.toFixed(value < 10 ? 2 : 1)} ms`
}

export function formatBytes(value: number): string {
  if (!Number.isFinite(value) || value <= 0) {
    return '—'
  }
  const units = ['B', 'KB', 'MB', 'GB']
  let size = value
  let unit = 0
  while (size >= 1024 && unit < units.length - 1) {
    size /= 1024
    unit += 1
  }
  return `${size.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`
}

export function formatDate(value: string): string {
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) {
    return value
  }
  return new Intl.DateTimeFormat('en-US', {
    month: 'short',
    day: 'numeric',
    year: 'numeric',
  }).format(date)
}

export function formatRelative(value: string): string {
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) {
    return value
  }
  const diffSeconds = Math.round((date.getTime() - Date.now()) / 1000)
  const ranges: Array<[Intl.RelativeTimeFormatUnit, number]> = [
    ['year', 60 * 60 * 24 * 365],
    ['month', 60 * 60 * 24 * 30],
    ['week', 60 * 60 * 24 * 7],
    ['day', 60 * 60 * 24],
    ['hour', 60 * 60],
    ['minute', 60],
  ]
  const formatter = new Intl.RelativeTimeFormat('en-US', { numeric: 'auto' })
  for (const [unit, seconds] of ranges) {
    if (Math.abs(diffSeconds) >= seconds) {
      return formatter.format(Math.round(diffSeconds / seconds), unit)
    }
  }
  return 'just now'
}

export function truncate(value: string, length: number): string {
  if (value.length <= length) {
    return value
  }
  const cut = value.slice(0, length)
  const space = cut.lastIndexOf(' ')
  return `${cut.slice(0, space > length * 0.6 ? space : length)}…`
}

export function fileKind(source: string): 'PDF' | 'Markdown' | 'Text' {
  const extension = source.split('.').pop()?.toLowerCase() ?? ''
  if (extension === 'pdf') {
    return 'PDF'
  }
  if (extension === 'md' || extension === 'markdown') {
    return 'Markdown'
  }
  return 'Text'
}

export function pluralize(count: number, singular: string, plural = `${singular}s`): string {
  return `${formatNumber(count)} ${count === 1 ? singular : plural}`
}

export function cleanAnswer(value: string): string {
  return value.trim()
}
