import { useCallback, useEffect, useMemo, useState } from 'react'
import { api } from '../../api/client'
import type { MapResponse } from '../../api/types'
import { useData } from '../../app/DataProvider'
import { buildDocumentColors } from '../../lib/colors'
import { formatNumber } from '../../lib/format'
import { Button } from '../../ui/Button'
import { EmptyState, ErrorNotice, Skeleton } from '../../ui/States'
import { Icon } from '../../ui/Icon'
import { PageHeader } from '../../ui/PageHeader'
import { MapInspector, QueryCard } from './MapInspector'
import { Plot2D } from './Plot2D'
import { Plot3D } from './Plot3D'
import { useProjection } from './useProjection'

type ViewMode = '2d' | '3d'

export function MapPage({ onOpenDocuments }: { onOpenDocuments: () => void }) {
  const { lastAnswer, lastQuestion, status } = useData()
  const [map, setMap] = useState<MapResponse | null>(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)
  const [reload, setReload] = useState(0)
  const [mode, setMode] = useState<ViewMode>('2d')
  const [selectedId, setSelectedId] = useState<number | null>(null)
  const [hoveredId, setHoveredId] = useState<number | null>(null)
  const [focusedDocument, setFocusedDocument] = useState<number | null>(null)
  const [showQuery, setShowQuery] = useState(true)

  useEffect(() => {
    let active = true
    setLoading(true)
    setError(null)
    api
      .map(1200)
      .then((response) => {
        if (active) {
          setMap(response)
        }
      })
      .catch((requestError: unknown) => {
        if (active) {
          setError(
            requestError instanceof Error
              ? requestError.message
              : 'The embedding map could not be loaded.',
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

  const points = map?.points ?? []
  const query = lastAnswer?.query_vector ?? null
  const { projection, projecting } = useProjection(points, query)

  useEffect(() => {
    if (!map) {
      return
    }
    const chunkIds = new Set(map.points.map((point) => point.chunk_id))
    const documentIds = new Set(map.points.map((point) => point.document_id))
    setSelectedId((current) => (current !== null && !chunkIds.has(current) ? null : current))
    setFocusedDocument((current) => (current !== null && !documentIds.has(current) ? null : current))
  }, [map])

  const colors = useMemo(
    () => buildDocumentColors(points.map((point) => point.document_id)),
    [points],
  )
  const colorFor = useCallback(
    (documentId: number) => colors.get(documentId) ?? '#7a847d',
    [colors],
  )

  const plottedIds = useMemo(() => new Set(points.map((point) => point.chunk_id)), [points])
  const highlighted = useMemo(
    () =>
      new Set(
        lastAnswer?.sources
          .map((source) => source.chunk_id)
          .filter((chunkId) => plottedIds.has(chunkId)) ?? [],
      ),
    [lastAnswer, plottedIds],
  )

  const legend = useMemo(() => {
    const entries = new Map<number, { title: string; count: number }>()
    for (const point of points) {
      const current = entries.get(point.document_id)
      if (current) {
        current.count += 1
      } else {
        entries.set(point.document_id, { title: point.title, count: 1 })
      }
    }
    return [...entries.entries()].sort((a, b) => a[0] - b[0])
  }, [points])

  const inspected = points.find((point) => point.chunk_id === (selectedId ?? hoveredId)) ?? null
  const inspectedIndex = inspected
    ? points.findIndex((point) => point.chunk_id === inspected.chunk_id)
    : -1
  const inspectedSource = inspected
    ? (lastAnswer?.sources.findIndex((source) => source.chunk_id === inspected.chunk_id) ?? -1)
    : -1
  const inspectedCoordinates =
    inspected && projection && inspectedIndex >= 0 ? projection.points[inspectedIndex] : null
  const dimension = projection?.mean.length ?? status?.embedding.dim ?? 0
  const canPlotQuery = Boolean(projection?.query)
  // One source of truth for the toggle, the legend, and the plot itself. When
  // the saved query has a different dimension it is not projected, so showing
  // half an overlay would imply a relationship that is not on screen.
  const overlayActive = showQuery && Boolean(lastAnswer) && canPlotQuery
  const hitCount = highlighted.size

  const refresh = () => setReload((value) => value + 1)

  const focusDocument = (documentId: number) => {
    setFocusedDocument((current) => (current === documentId ? null : documentId))
    setSelectedId(null)
  }

  const renderPlot = () => {
    if (!projection) {
      return null
    }
    const shared = {
      points,
      coordinates: projection.points,
      query: projection.query,
      colorFor,
      highlighted,
      selectedId,
      hoveredId,
      focusedDocument,
      showQuery: overlayActive,
      describedBy: 'map-accessible-note',
      onSelect: setSelectedId,
      onHover: setHoveredId,
    }
    return mode === '3d' ? <Plot3D {...shared} /> : <Plot2D {...shared} />
  }

  return (
    <div className="page">
      <PageHeader
        eyebrow="Embedding space"
        title="The shape of your knowledge"
        description="Every chunk is a point in high-dimensional space. This map projects a sample of up to 1,200 stored embeddings to two or three dimensions with PCA, so clusters, outliers, and retrieval neighbourhoods become visible."
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

      {loading && !map ? (
        <div className="map-loading" role="status" aria-busy="true" aria-label="Loading the embedding map">
          <Skeleton className="map-loading__canvas" />
          <Skeleton className="map-loading__panel" />
        </div>
      ) : map && map.total_chunks === 0 ? (
        <EmptyState
          icon="map"
          title="Nothing to map yet"
          description="Upload a document and its chunks will appear here as points in embedding space."
          action={
            <Button variant="primary" onClick={onOpenDocuments} icon={<Icon name="documents" size={16} />}>
              Go to documents
            </Button>
          }
        />
      ) : map && map.points.length === 0 ? (
        <EmptyState
          icon="map"
          title="No embeddings to map"
          description="The library has chunks, but none of them returned a stored embedding. Re-embed the library or refresh to try again."
          action={<Button variant="secondary" onClick={refresh}>Refresh map</Button>}
        />
      ) : projecting || !projection ? (
        <div
          className="map-loading"
          role="status"
          aria-busy="true"
          aria-label="Projecting embeddings into two dimensions"
        >
          <Skeleton className="map-loading__canvas" />
          <Skeleton className="map-loading__panel" />
        </div>
      ) : (
        <>
          <div className="map-toolbar">
            <div className="segmented" role="group" aria-label="Map dimensionality">
              <button
                type="button"
                className={mode === '2d' ? 'segmented__item is-active' : 'segmented__item'}
                aria-pressed={mode === '2d'}
                onClick={() => setMode('2d')}
              >
                <Icon name="grid" size={15} /> 2D
              </button>
              <button
                type="button"
                className={mode === '3d' ? 'segmented__item is-active' : 'segmented__item'}
                aria-pressed={mode === '3d'}
                onClick={() => setMode('3d')}
              >
                <Icon name="cube" size={15} /> 3D
              </button>
            </div>

            <span className="map-toolbar__rule" />

            <button
              type="button"
              className={`toggle ${overlayActive ? 'is-active' : ''}`}
              onClick={() => setShowQuery((value) => !value)}
              aria-pressed={overlayActive}
              disabled={!lastAnswer || !canPlotQuery}
              title={
                !lastAnswer
                  ? 'Ask a question to see retrieval highlights'
                  : !canPlotQuery
                    ? 'The saved query embedding does not match this map dimension; ask again to plot it'
                    : 'Highlight the chunks and query embedding from your latest question'
              }
            >
              <span className="toggle__dot" />
              Query overlay
            </button>

            <span className="map-toolbar__meta">
              {formatNumber(points.length)} of {formatNumber(map?.total_chunks ?? 0)} chunks · {dimension}-d
              → {mode === '3d' ? '3D' : '2D'} PCA
            </span>
          </div>

          <div className="map-layout">
            <section className="map-panel" aria-label="Embedding projection">
              <div className="map-panel__head">
                <div>
                  <h2>{mode === '3d' ? '3D projection' : '2D projection'}</h2>
                  <p>Points are coloured by document. Hover or select any point to inspect its chunk.</p>
                </div>
                <div className="map-panel__annotations">
                  {overlayActive ? (
                    <>
                      <span className="annotation">
                        <span className="marker marker--ring" />
                        retrieved
                      </span>
                      <span className="annotation">
                        <span className="marker marker--diamond" />
                        query
                      </span>
                    </>
                  ) : null}
                </div>
              </div>

              <div className="map-panel__canvas">{renderPlot()}</div>
              <p id="map-accessible-note" className="sr-only">
                The projection is a visual overview. To browse the same points with a keyboard or
                screen reader, use the “Inspect a chunk” list in the map inspector.
              </p>

              <div className="map-panel__foot">
                <div className="legend" role="group" aria-label="Documents in the map">
                  {legend.map(([documentId, entry]) => (
                    <button
                      key={documentId}
                      type="button"
                      className={`legend__item ${focusedDocument === documentId ? 'is-active' : ''}`}
                      onClick={() => focusDocument(documentId)}
                      aria-pressed={focusedDocument === documentId}
                      title={`Focus ${entry.title}`}
                    >
                      <span className="legend__swatch" style={{ background: colorFor(documentId) }} />
                      <span className="legend__name">{entry.title}</span>
                      <span className="legend__count">{entry.count}</span>
                    </button>
                  ))}
                </div>
                {focusedDocument !== null ? (
                  <button type="button" className="legend__clear" onClick={() => setFocusedDocument(null)}>
                    Show all
                  </button>
                ) : null}
              </div>
            </section>

            <aside className="map-side" aria-label="Map inspector">
              <MapInspector
                inspected={inspected}
                coordinates={inspectedCoordinates}
                color={inspected ? colorFor(inspected.document_id) : 'transparent'}
                sourceIndex={inspectedSource}
                focused={inspected !== null && focusedDocument === inspected.document_id}
                points={points}
                selectedId={selectedId}
                onSelect={(id) => {
                  setSelectedId(id)
                  if (id !== null) {
                    setFocusedDocument(null)
                  }
                }}
                onClear={() => {
                  setSelectedId(null)
                  setHoveredId(null)
                }}
              />

              <QueryCard
                lastAnswer={lastAnswer}
                lastQuestion={lastQuestion}
                showQuery={overlayActive}
                onToggleQuery={() => setShowQuery((value) => !value)}
                queryPlotted={canPlotQuery}
                hitCount={hitCount}
              />

              <div className="map-note">
                <Icon name="info" size={16} />
                <p>
                  PCA is fitted in your browser on up to 1,200 stored embeddings. It preserves the
                  dominant variance, not every neighbour relation — use Ask to see the actual HNSW
                  result set. On-screen coordinates are rescaled to a unit view, so only relative
                  position is meaningful.
                </p>
              </div>
            </aside>
          </div>
        </>
      )}
    </div>
  )
}
