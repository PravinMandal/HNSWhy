import type { BenchMethod, BenchReport } from '../../api/types'
import { formatNumber, formatPercent, pluralize } from '../../lib/format'
import { Badge } from '../../ui/Badge'
import { Icon } from '../../ui/Icon'
import { LatencyBars } from './LatencyBars'
import { MethodTable } from './MethodTable'
import { formatDuration } from './metrics'
import { RecallChart } from './RecallChart'

export function ReportDetails({
  report,
  hnsw,
  active,
  brute,
  activeEf,
  speedup,
  recallRetention,
  p50Speedup,
  onSelectEf,
}: {
  report: BenchReport
  hnsw: BenchMethod[]
  active: BenchMethod | null
  brute: BenchMethod | undefined
  activeEf: number | null
  speedup: number | null
  recallRetention: number | null
  p50Speedup: number | null
  onSelectEf: (ef: number) => void
}) {
  return (
    <div className="bench-grid">
      <section className="card card--wide" aria-labelledby="recall-heading">
        <div className="card__head">
          <div>
            <h2 id="recall-heading">Recall vs. efSearch</h2>
            <p>
              Search quality against the exact ground truth for the same{' '}
              {formatNumber(report.dataset.query_count)} queries.
            </p>
          </div>
          <div className="ef-picker" role="group" aria-label="Select efSearch">
            {hnsw.map((method) => (
              <button
                key={method.ef_search}
                type="button"
                className={method.ef_search === activeEf ? 'is-active' : ''}
                onClick={() => method.ef_search !== undefined && onSelectEf(method.ef_search)}
                aria-pressed={method.ef_search === activeEf}
              >
                {method.ef_search}
              </button>
            ))}
          </div>
        </div>
        <RecallChart methods={hnsw} activeEf={activeEf} onSelect={onSelectEf} />
      </section>

      <section className="card" aria-labelledby="latency-heading">
        <div className="card__head">
          <div>
            <h2 id="latency-heading">Latency percentiles</h2>
            <p>
              {activeEf !== null
                ? `Per-query search time at efSearch ${activeEf}.`
                : 'The report has no HNSW sweep points.'}
            </p>
          </div>
          <Icon name="clock" size={17} />
        </div>
        {active ? (
          <LatencyBars method={active} />
        ) : (
          <p className="muted">No HNSW measurements in this report.</p>
        )}
        <div className="card__foot">
          <span>Average distance computations</span>
          <strong>{active ? formatNumber(active.avg_distance_computations) : '—'}</strong>
        </div>
      </section>

      <section className="card card--wide" aria-labelledby="table-heading">
        <div className="card__head">
          <div>
            <h2 id="table-heading">All measurements</h2>
            <p>HNSW rows are sweep points; the exact scan is the baseline.</p>
          </div>
          <Badge tone="neutral">{pluralize(report.methods.length, 'row')}</Badge>
        </div>
        <MethodTable methods={report.methods} activeEf={activeEf} onSelect={onSelectEf} />
      </section>

      {active && brute ? (
        <section className="card card--comparison" aria-labelledby="comparison-heading">
          <div className="card__head">
            <div>
              <h2 id="comparison-heading">HNSW vs. exact scan</h2>
              <p>Same dataset, same queries, same distance function.</p>
            </div>
          </div>
          <div className="comparison">
            <div>
              <span>Throughput ratio</span>
              <strong>{speedup !== null ? `${speedup.toFixed(1)}×` : '—'}</strong>
              <small>
                {formatNumber(active.qps)} QPS vs. {formatNumber(brute.qps)} QPS
              </small>
            </div>
            <div>
              <span>Top-10 recall retained</span>
              <strong>{recallRetention !== null ? formatPercent(recallRetention, 1) : '—'}</strong>
              <small>
                {formatPercent(active.recall_at_10, 2)} vs. {formatPercent(brute.recall_at_10, 2)} exact
              </small>
            </div>
            <div>
              <span>p50 speedup</span>
              <strong>{p50Speedup !== null ? `${p50Speedup.toFixed(1)}×` : '—'}</strong>
              <small>
                {formatDuration(active.latency_ms.p50)} vs. {formatDuration(brute.latency_ms.p50)} p50
              </small>
            </div>
          </div>
        </section>
      ) : null}

      <section className="card" aria-labelledby="config-heading">
        <div className="card__head">
          <div>
            <h2 id="config-heading">Build and index configuration</h2>
            <p>Parameters used to build the graph.</p>
          </div>
          <Icon name="layers" size={17} />
        </div>
        <dl className="detail-list">
          <div><dt>M</dt><dd>{report.config.M}</dd></div>
          <div><dt>M0</dt><dd>{report.config.M0}</dd></div>
          <div><dt>efConstruction</dt><dd>{report.config.ef_construction}</dd></div>
          <div><dt>Distance</dt><dd>{report.config.distance}</dd></div>
          <div><dt>Seed</dt><dd>{report.config.seed}</dd></div>
          <div><dt>k</dt><dd>{report.config.k}</dd></div>
          <div><dt>Passes</dt><dd>{report.config.passes}</dd></div>
          <div><dt>Warmup queries</dt><dd>{report.config.warmup_queries}</dd></div>
        </dl>
      </section>

      <section className="card" aria-labelledby="environment-heading">
        <div className="card__head">
          <div>
            <h2 id="environment-heading">Environment</h2>
            <p>Reproducibility details recorded with the run.</p>
          </div>
          <Icon name="cpu" size={17} />
        </div>
        <dl className="detail-list detail-list--stacked">
          <div><dt>Dataset</dt><dd>{report.dataset.base_file}</dd></div>
          <div><dt>Compiler</dt><dd>{report.environment.compiler}</dd></div>
          <div><dt>CPU</dt><dd>{report.environment.cpu}</dd></div>
          <div><dt>Threads</dt><dd>{report.environment.hardware_threads}</dd></div>
          <div><dt>Build</dt><dd>{report.config.build_type}</dd></div>
          <div><dt>AVX2</dt><dd>{report.environment.avx2 ? 'enabled' : 'not used'}</dd></div>
          <div><dt>Engine</dt><dd>{report.engine.name} {report.engine.version}</dd></div>
          {report.provenance?.run_id ? (
            <div><dt>Run</dt><dd>{report.provenance.run_id}</dd></div>
          ) : null}
          {report.provenance?.git_commit ? (
            <div>
              <dt>Commit</dt>
              <dd>
                {report.provenance.git_commit.slice(0, 12)}
                {report.provenance.worktree_dirty ? ' (dirty)' : ''}
              </dd>
            </div>
          ) : null}
          {report.provenance?.cxx_flags_release ? (
            <div><dt>Release flags</dt><dd>{report.provenance.cxx_flags_release}</dd></div>
          ) : null}
        </dl>
      </section>
    </div>
  )
}
