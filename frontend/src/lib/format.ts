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
