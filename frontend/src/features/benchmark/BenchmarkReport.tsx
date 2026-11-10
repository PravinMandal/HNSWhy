import { useMemo, useState } from 'react'
import type { BenchReport } from '../../api/types'
import { formatNumber, formatPercent } from '../../lib/format'
import { Badge } from '../../ui/Badge'
import { Button } from '../../ui/Button'
import { Icon } from '../../ui/Icon'
import { MetricCard } from '../../ui/PageHeader'
import { formatDuration, formatRate, ReportDetails } from './ReportDetails'

function efOf(method: { ef_search?: number }): number {
  return method.ef_search ?? 0
}

export function BenchmarkReport({ report, onRefresh }: { report: BenchReport; onRefresh: () => void }) {
  const hnsw = useMemo(
    () => report.methods.filter((method) => method.name === 'hnsw').sort((a, b) => efOf(a) - efOf(b)),
    [report],
  )
  const brute = report.methods.find((method) => method.name === 'bruteforce')
  const [ef, setEf] = useState<number | null>(null)
  const activeEf =
    ef !== null && hnsw.some((method) => efOf(method) === ef)
      ? ef
      : hnsw.find((method) => efOf(method) === 50)?.ef_search ??
        hnsw[hnsw.length - 1]?.ef_search ??
        null
  const active = hnsw.find((method) => efOf(method) === activeEf) ?? null
  const speedup = active && brute && brute.qps > 0 ? active.qps / brute.qps : null
  const recallRetention =
    active && brute && brute.recall_at_10 > 0 ? active.recall_at_10 / brute.recall_at_10 : null
  const latencyReduction =
    active && brute && active.latency_ms.p50 > 0 && brute.latency_ms.p50 > 0
      ? brute.latency_ms.p50 / active.latency_ms.p50
      : null
  const headline =
    active && brute && speedup !== null && recallRetention !== null
      ? `${speedup.toFixed(1)}× the throughput of the exact scan while retaining ${formatPercent(
          recallRetention,
          1,
        )} of its top-10 results.`
      : 'Swap efSearch to trade latency for recall. Every value below comes from the stored benchmark report.'

  const download = () => {
    const blob = new Blob([JSON.stringify(report, null, 2)], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const link = document.createElement('a')
    link.href = url
    link.download = 'bench.json'
    link.click()
    URL.revokeObjectURL(url)
  }

  return (
    <>
      <div className="report-meta">
        <div className="report-meta__left">
          <Badge tone="accent" dot>measured report</Badge>
          <span>{report.dataset.name}</span>
          <span className="report-meta__sep">/</span>
          <span>{formatNumber(report.dataset.base_count)} base vectors</span>
          <span className="report-meta__sep">/</span>
          <span>{formatNumber(report.dataset.query_count)} queries</span>
          <span className="report-meta__sep">/</span>
          <span>{report.dataset.dim}-d</span>
        </div>
        <div className="report-meta__right">
          <span>Generated {new Date(report.generated_at_utc).toLocaleString()}</span>
          <Button variant="ghost" size="sm" onClick={download} icon={<Icon name="external" size={14} />}>
            Download JSON
          </Button>
          <Button variant="ghost" size="sm" onClick={onRefresh} icon={<Icon name="refresh" size={14} />}>
            Reload
          </Button>
        </div>
      </div>

      <section className="report-hero" aria-label="Headline results">
        <div className="report-hero__intro">
          <span className="eyebrow">HNSW at efSearch {activeEf}</span>
          <h2>
            <span>{active ? formatPercent(active.recall_at_10, 2) : '—'}</span> recall@10
          </h2>
          <p>{headline}</p>
        </div>
        <div className="report-hero__metrics">
          <MetricCard
            icon="search"
            label="Recall@1"
            value={active ? formatPercent(active.recall_at_1, 2) : '—'}
            detail="at selected efSearch"
          />
          <MetricCard
            icon="chart"
            label="Throughput"
            value={active ? formatRate(active.qps) : '—'}
            detail="queries per second"
            tone="accent"
          />
          <MetricCard
            icon="clock"
            label="p50 latency"
            value={active ? formatDuration(active.latency_ms.p50) : '—'}
            detail={active ? `p95 ${formatDuration(active.latency_ms.p95)}` : undefined}
          />
          <MetricCard
            icon="cpu"
            label="Build time"
            value={formatDuration(report.build.build_time_ms)}
            detail={`${report.build.peak_rss_mb.toFixed(1)} MB peak RSS`}
          />
        </div>
      </section>

      <ReportDetails
        report={report}
        hnsw={hnsw}
        active={active}
        brute={brute}
        activeEf={activeEf}
        speedup={speedup}
        recallRetention={recallRetention}
        latencyReduction={latencyReduction}
        onSelectEf={setEf}
      />

      <p className="bench-footnote">
        This page renders <code>docs/results/bench.json</code> exactly as produced by the standalone
        benchmark binary. It does not run measurements in the browser and never substitutes
        estimates for missing values.
      </p>
    </>
  )
}
