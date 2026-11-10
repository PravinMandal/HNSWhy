import type { BenchMethod } from '../../api/types'
import { formatNumber, formatPercent } from '../../lib/format'
import { efOf, formatDuration } from './metrics'

export function MethodTable({
  methods,
  activeEf,
  onSelect,
}: {
  methods: BenchMethod[]
  activeEf: number | null
  onSelect: (ef: number) => void
}) {
  return (
    <div className="table-wrap">
      <table className="data-table">
        <thead>
          <tr>
            <th scope="col">Method</th>
            <th scope="col">Recall@1</th>
            <th scope="col">Recall@10</th>
            <th scope="col">QPS</th>
            <th scope="col">p50</th>
            <th scope="col">p95</th>
            <th scope="col">p99</th>
            <th scope="col">Avg. distance computations</th>
          </tr>
        </thead>
        <tbody>
          {methods.map((method) => {
            const active = method.name === 'hnsw' && efOf(method) === activeEf
            return (
              <tr
                key={`${method.name}-${method.ef_search ?? 'exact'}`}
                className={active ? 'is-active' : method.name === 'bruteforce' ? 'is-baseline' : ''}
              >
                <th scope="row">
                  {method.name === 'bruteforce' ? (
                    <span className="table-method">
                      Brute force <span>exact baseline</span>
                    </span>
                  ) : (
                    <button
                      type="button"
                      className="table-method table-method--button"
                      onClick={() => onSelect(efOf(method))}
                      aria-pressed={active}
                    >
                      HNSW <span>efSearch {method.ef_search}</span>
                    </button>
                  )}
                </th>
                <td>{formatPercent(method.recall_at_1, 2)}</td>
                <td>{formatPercent(method.recall_at_10, 2)}</td>
                <td>{formatNumber(method.qps)}</td>
                <td>{formatDuration(method.latency_ms.p50)}</td>
                <td>{formatDuration(method.latency_ms.p95)}</td>
                <td>{formatDuration(method.latency_ms.p99)}</td>
                <td>{formatNumber(method.avg_distance_computations)}</td>
              </tr>
            )
          })}
        </tbody>
      </table>
    </div>
  )
}
