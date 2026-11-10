import type { BenchMethod } from '../../api/types'

function formatDuration(ms: number): string {
  if (!Number.isFinite(ms)) {
    return '—'
  }
  if (ms >= 1000) {
    return `${(ms / 1000).toFixed(2)} s`
  }
  return `${ms.toFixed(ms < 10 ? 2 : 1)} ms`
}

export function LatencyBars({ method }: { method: BenchMethod }) {
  const values = [
    { label: 'p50', value: method.latency_ms.p50 },
    { label: 'p95', value: method.latency_ms.p95 },
    { label: 'p99', value: method.latency_ms.p99 },
  ]
  const maximum = Math.max(...values.map((entry) => entry.value), 0.0001)

  return (
    <div className="latency">
      {values.map((entry) => (
        <div key={entry.label} className="latency__row">
          <span className="latency__label">{entry.label}</span>
          <div className="latency__track">
            <span style={{ width: `${Math.max(3, (entry.value / maximum) * 100)}%` }} />
          </div>
          <strong>{formatDuration(entry.value)}</strong>
        </div>
      ))}
    </div>
  )
}
