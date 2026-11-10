import type { BenchMethod } from '../../api/types'
import { formatPercent } from '../../lib/format'

export function RecallChart({
  methods,
  activeEf,
  onSelect,
}: {
  methods: BenchMethod[]
  activeEf: number | null
  onSelect: (ef: number) => void
}) {
  const width = 720
  const height = 300
  const padding = { top: 26, right: 28, bottom: 48, left: 58 }
  const plotWidth = width - padding.left - padding.right
  const plotHeight = height - padding.top - padding.bottom
  const values = methods.flatMap((method) => [method.recall_at_1, method.recall_at_10])
  const minimum = values.length ? Math.max(0, Math.floor(Math.min(...values) * 20) / 20 - 0.05) : 0.8
  const maximum = 1

  const x = (index: number) =>
    padding.left +
    (methods.length <= 1 ? plotWidth / 2 : (index / (methods.length - 1)) * plotWidth)
  const y = (value: number) =>
    padding.top + (1 - (value - minimum) / (maximum - minimum || 1)) * plotHeight
  const pathFor = (key: 'recall_at_1' | 'recall_at_10') =>
    methods
      .map(
        (method, index) =>
          `${index === 0 ? 'M' : 'L'} ${x(index).toFixed(2)} ${y(method[key]).toFixed(2)}`,
      )
      .join(' ')
  const ticks = Array.from({ length: 5 }, (_, index) => minimum + ((maximum - minimum) / 4) * index)
  const hitWidth = Math.max(24, plotWidth / Math.max(methods.length, 1) / 1.4)

  return (
    <figure className="chart">
      <svg
        viewBox={`0 0 ${width} ${height}`}
        className="chart__svg"
        role="img"
        aria-label="Recall at one and recall at ten as efSearch increases. Values are also in the results table."
      >
        {ticks.map((tick) => (
          <g key={tick}>
            <line
              className="chart__grid"
              x1={padding.left}
              y1={y(tick)}
              x2={width - padding.right}
              y2={y(tick)}
            />
            <text className="chart__tick" x={padding.left - 10} y={y(tick) + 4} textAnchor="end">
              {formatPercent(tick, 0)}
            </text>
          </g>
        ))}

        {methods.map((method, index) => (
          <g key={method.ef_search ?? index}>
            {method.ef_search === activeEf ? (
              <line
                className="chart__active"
                x1={x(index)}
                y1={padding.top}
                x2={x(index)}
                y2={height - padding.bottom}
              />
            ) : null}
            <text
              className="chart__tick"
              x={x(index)}
              y={height - padding.bottom + 22}
              textAnchor="middle"
            >
              {method.ef_search}
            </text>
            <rect
              x={x(index) - hitWidth / 2}
              y={padding.top}
              width={hitWidth}
              height={plotHeight}
              fill="transparent"
              className="chart__hit"
              onClick={() => method.ef_search !== undefined && onSelect(method.ef_search)}
            >
              <title>Select efSearch {method.ef_search}</title>
            </rect>
          </g>
        ))}

        <text className="chart__axis" x={width / 2} y={height - 8} textAnchor="middle">
          efSearch
        </text>
        <text
          className="chart__axis"
          x={14}
          y={height / 2}
          textAnchor="middle"
          transform={`rotate(-90 14 ${height / 2})`}
        >
          recall
        </text>

        <path d={pathFor('recall_at_1')} className="chart__line chart__line--r1" />
        <path d={pathFor('recall_at_10')} className="chart__line chart__line--r10" />

        {methods.map((method, index) => (
          <g key={`point-${method.ef_search ?? index}`}>
            <circle
              cx={x(index)}
              cy={y(method.recall_at_1)}
              r={method.ef_search === activeEf ? 5.2 : 3.4}
              className="chart__point chart__point--r1"
              onClick={() => method.ef_search !== undefined && onSelect(method.ef_search)}
            >
              <title>
                R@1 {formatPercent(method.recall_at_1, 2)} at efSearch {method.ef_search}
              </title>
            </circle>
            <circle
              cx={x(index)}
              cy={y(method.recall_at_10)}
              r={method.ef_search === activeEf ? 5.2 : 3.4}
              className="chart__point chart__point--r10"
              onClick={() => method.ef_search !== undefined && onSelect(method.ef_search)}
            >
              <title>
                R@10 {formatPercent(method.recall_at_10, 2)} at efSearch {method.ef_search}
              </title>
            </circle>
          </g>
        ))}
      </svg>

      <figcaption className="chart__legend">
        <span>
          <span className="chart__swatch chart__swatch--r1" /> Recall@1
        </span>
        <span>
          <span className="chart__swatch chart__swatch--r10" /> Recall@10
        </span>
        <span className="chart__legend-note">Click a point to switch the highlighted efSearch.</span>
      </figcaption>
    </figure>
  )
}
