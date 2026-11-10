import { useEffect, useState } from 'react'
import { api } from '../../api/client'
import type { BenchmarkResponse } from '../../api/types'
import { Button } from '../../ui/Button'
import { ErrorNotice, Skeleton } from '../../ui/States'
import { Icon } from '../../ui/Icon'
import { PageHeader } from '../../ui/PageHeader'
import { BenchmarkReport } from './BenchmarkReport'

export function BenchmarkPage() {
  const [response, setResponse] = useState<BenchmarkResponse | null>(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)
  const [reload, setReload] = useState(0)

  useEffect(() => {
    let active = true
    setLoading(true)
    setError(null)
    api
      .benchmark()
      .then((next) => {
        if (active) {
          setResponse(next)
        }
      })
      .catch((requestError: unknown) => {
        if (active) {
          setError(
            requestError instanceof Error
              ? requestError.message
              : 'The benchmark report could not be loaded.',
          )
        }
      })
      .finally(() => {
        if (active) {
          setLoading(false)
        }
      })
    return () => {
      active = false
    }
  }, [reload])

  const refresh = () => setReload((value) => value + 1)

  return (
    <div className="page">
      <PageHeader
        eyebrow="Benchmark"
        title="Measured, not estimated"
        description="Results from the standalone HNSW benchmark: exact ground truth, recall, throughput, latency, build time, and memory. The dataset and efSearch sweep come from the stored report; nothing here is estimated."
        actions={
          <Button
            variant="secondary"
            onClick={refresh}
            loading={loading}
            icon={<Icon name="refresh" size={15} />}
          >
            Refresh
          </Button>
        }
      />

      {error ? <ErrorNotice message={error} retry={refresh} /> : null}

      {loading && !response ? (
        <div className="bench-loading">
          <Skeleton className="skeleton--block skeleton--h-180" />
          <div className="bench-loading__grid">
            <Skeleton className="skeleton--block skeleton--h-300" />
            <Skeleton className="skeleton--block skeleton--h-300" />
          </div>
        </div>
      ) : response?.available ? (
        <BenchmarkReport report={response.report} onRefresh={refresh} />
      ) : response ? (
        <section className="bench-unavailable" aria-labelledby="bench-unavailable-heading">
          <span className="bench-unavailable__icon">
            <Icon name="chart" size={24} />
          </span>
          <div>
            <h2 id="bench-unavailable-heading">No benchmark report found</h2>
            <p>{response.message}</p>
            <p>Run the independent benchmark against SIFT-100k to produce the report:</p>
            <pre>
              <code>{response.command}</code>
            </pre>
            <p className="muted">
              The report is written to <code>docs/results/bench.json</code> and served read-only by
              the API. Real numbers only — this page stays empty until a run exists.
            </p>
            <Button variant="secondary" onClick={refresh} icon={<Icon name="refresh" size={15} />}>
              Check again
            </Button>
          </div>
        </section>
      ) : null}
    </div>
  )
}
