import { formatNumber } from '../../lib/format'

export function efOf(method: { ef_search?: number }): number {
  return method.ef_search ?? 0
}

export function formatDuration(ms: number): string {
  if (!Number.isFinite(ms)) {
    return '—'
  }
  if (ms >= 1000) {
    return `${(ms / 1000).toFixed(2)} s`
  }
  return `${ms.toFixed(ms < 10 ? 2 : 1)} ms`
}

export function formatRate(value: number): string {
  if (!Number.isFinite(value)) {
    return '—'
  }
  return `${formatNumber(value)}/s`
}
