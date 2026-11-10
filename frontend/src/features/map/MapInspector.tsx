import type { AskResponse, MapPoint } from '../../api/types'
import { pluralize, truncate } from '../../lib/format'
import { Badge } from '../../ui/Badge'
import { Button } from '../../ui/Button'
import { Icon } from '../../ui/Icon'

export function MapInspector({
  inspected,
  coordinates,
  color,
  sourceIndex,
  focused,
  points,
  selectedId,
  onSelect,
  onClear,
}: {
  inspected: MapPoint | null
  coordinates: number[] | null
  color: string
  sourceIndex: number
  focused: boolean
  points: MapPoint[]
  selectedId: number | null
  onSelect: (id: number | null) => void
  onClear: () => void
}) {
  return (
    <div className="inspector">
      <div className="inspector__head">
        <span className="eyebrow">Point inspector</span>
        <Icon name="search" size={16} />
      </div>

      {inspected ? (
        <div className="point-detail">
          <div className="point-detail__title">
            <span className="point-detail__swatch" style={{ background: color }} />
            <div>
              <h3>{inspected.title}</h3>
              <span>
                chunk #{inspected.chunk_id} · position {inspected.ordinal + 1}
              </span>
            </div>
          </div>

          <div className="point-detail__badges">
            {sourceIndex >= 0 ? (
              <Badge tone="accent">retrieved source [{sourceIndex + 1}]</Badge>
            ) : (
              <Badge tone="neutral">not retrieved</Badge>
            )}
            {focused ? <Badge tone="info">focused document</Badge> : null}
          </div>

          <p className="point-detail__preview">{truncate(inspected.preview, 320)}</p>

          <dl className="point-detail__meta">
            <div>
              <dt>PC1 (scaled)</dt>
              <dd>{coordinates?.[0]?.toFixed(3) ?? '—'}</dd>
            </div>
            <div>
              <dt>PC2 (scaled)</dt>
              <dd>{coordinates?.[1]?.toFixed(3) ?? '—'}</dd>
            </div>
            <div>
              <dt>PC3 (scaled)</dt>
              <dd>{coordinates?.[2]?.toFixed(3) ?? '—'}</dd>
            </div>
          </dl>
          <p className="point-detail__note">
            PCA scores are rescaled to a unit view for display, so read relative spacing rather
            than raw magnitude.
          </p>

          <Button variant="ghost" size="sm" onClick={onClear} icon={<Icon name="close" size={14} />}>
            Clear selection
          </Button>
        </div>
      ) : (
        <div className="point-detail point-detail--empty">
          <span className="point-detail__empty-icon">
            <Icon name="spark" size={20} />
          </span>
          <p>
            Select a point on the map — or choose one below — to inspect its chunk, document, and
            retrieval status.
          </p>
        </div>
      )}

      <label className="select select--stacked">
        <span>Inspect a chunk</span>
        <select
          value={selectedId ?? ''}
          onChange={(event) => onSelect(event.target.value ? Number(event.target.value) : null)}
        >
          <option value="">Choose a chunk…</option>
          {points.map((point) => (
            <option key={point.chunk_id} value={point.chunk_id}>
              {point.title} · #{point.chunk_id}
            </option>
          ))}
        </select>
      </label>
    </div>
  )
}

export function QueryCard({
  lastAnswer,
  lastQuestion,
  showQuery,
  onToggleQuery,
  queryPlotted,
  hitCount,
}: {
  lastAnswer: AskResponse | null
  lastQuestion: string
  showQuery: boolean
  onToggleQuery: () => void
  queryPlotted: boolean
  hitCount: number
}) {
  const hiddenHits = lastAnswer ? lastAnswer.sources.length - hitCount : 0
  return (
    <div className={`query-card ${lastAnswer ? '' : 'query-card--empty'}`}>
      <div className="query-card__head">
        <span className="eyebrow">Last query</span>
        {lastAnswer ? (
          <Badge tone="accent">
            {pluralize(hitCount, 'plotted hit')}
          </Badge>
        ) : null}
      </div>

      {lastAnswer ? (
        <>
          <p className="query-card__question">{lastQuestion || 'Previous question'}</p>
          <div className="query-card__legend">
            <span>
              <span className="marker marker--ring" /> retrieved chunks
            </span>
            <span>
              <span className="marker marker--diamond" /> query embedding
            </span>
          </div>
          {!queryPlotted ? (
            <p className="query-card__note">
              The saved query embedding has a different dimension than this map, so it cannot be
              projected without a re-embed. Ask again to place the current query.
            </p>
          ) : hiddenHits > 0 ? (
            <p className="query-card__note">
              {pluralize(hiddenHits, 'retrieved chunk')} fall outside this sample of{' '}
              {pluralize(hitCount, 'plotted chunk')}.
            </p>
          ) : null}
          <button type="button" className="query-card__toggle" onClick={onToggleQuery}>
            {showQuery ? 'Hide retrieval overlay' : 'Show retrieval overlay'}
          </button>
        </>
      ) : (
        <p className="query-card__note">
          Ask a question and the retrieved chunks will glow here, with your query plotted as a
          diamond in the same embedding space.
        </p>
      )}
    </div>
  )
}
