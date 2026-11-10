import { useEffect, useRef, useState, type KeyboardEvent, type PointerEvent } from 'react'
import type { MapPoint } from '../../api/types'
import { truncate } from '../../lib/format'

interface Plot2DProps {
  points: MapPoint[]
  coordinates: number[][]
  query: number[] | null
  colorFor: (documentId: number) => string
  highlighted: Set<number>
  selectedId: number | null
  hoveredId: number | null
  focusedDocument: number | null
  showQuery: boolean
  describedBy?: string
  onSelect: (id: number | null) => void
  onHover: (id: number | null) => void
}

const VIEW = 1.16

function contentBox(rect: DOMRect) {
  const side = Math.min(rect.width, rect.height)
  return {
    left: rect.left + (rect.width - side) / 2,
    top: rect.top + (rect.height - side) / 2,
    side,
  }
}

export function Plot2D({
  points,
  coordinates,
  query,
  colorFor,
  highlighted,
  selectedId,
  hoveredId,
  focusedDocument,
  showQuery,
  describedBy,
  onSelect,
  onHover,
}: Plot2DProps) {
  const svgRef = useRef<SVGSVGElement>(null)
  const dragRef = useRef<{
    pointerId: number
    startX: number
    startY: number
    originX: number
    originY: number
  } | null>(null)
  const scaleRef = useRef(1)
  const offsetRef = useRef({ x: 0, y: 0 })
  const [scale, setScale] = useState(1)
  const [offset, setOffset] = useState({ x: 0, y: 0 })
  const [viewport, setViewport] = useState({ width: 0, height: 0 })
  // Roving tab index: the map is one tab stop, and arrow keys walk the points.
  // Making all 1,200 circles tabbable would bury every other control on the page.
  const [focusIndex, setFocusIndex] = useState(0)

  useEffect(() => {
    const svg = svgRef.current
    if (!svg) {
      return
    }
    const observer = new ResizeObserver((entries) => {
      const rect = entries[0]?.contentRect
      if (rect) {
        setViewport({ width: rect.width, height: rect.height })
      }
    })
    observer.observe(svg)
    return () => observer.disconnect()
  }, [])

  const applyView = (nextScale: number, nextOffset: { x: number; y: number }) => {
    scaleRef.current = nextScale
    offsetRef.current = nextOffset
    setScale(nextScale)
    setOffset(nextOffset)
  }

  useEffect(() => {
    const svg = svgRef.current
    if (!svg) {
      return
    }
    const onWheel = (event: WheelEvent) => {
      event.preventDefault()
      const box = contentBox(svg.getBoundingClientRect())
      if (!box.side) {
        return
      }
      const cursor = {
        x: ((event.clientX - box.left) / box.side) * VIEW * 2 - VIEW,
        y: ((event.clientY - box.top) / box.side) * VIEW * 2 - VIEW,
      }
      const currentScale = scaleRef.current
      const currentOffset = offsetRef.current
      const anchor = {
        x: (cursor.x - currentOffset.x) / currentScale,
        y: (cursor.y - currentOffset.y) / currentScale,
      }
      const nextScale = Math.min(4, Math.max(0.7, currentScale * (event.deltaY > 0 ? 0.88 : 1.14)))
      applyView(nextScale, {
        x: cursor.x - anchor.x * nextScale,
        y: cursor.y - anchor.y * nextScale,
      })
    }
    svg.addEventListener('wheel', onWheel, { passive: false })
    return () => svg.removeEventListener('wheel', onWheel)
  }, [])

  const onPointerDown = (event: PointerEvent<SVGSVGElement>) => {
    if (event.button !== 0) {
      return
    }
    dragRef.current = {
      pointerId: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      originX: offset.x,
      originY: offset.y,
    }
    event.currentTarget.setPointerCapture(event.pointerId)
  }

  const onPointerMove = (event: PointerEvent<SVGSVGElement>) => {
    const drag = dragRef.current
    if (!drag || drag.pointerId !== event.pointerId) {
      return
    }
    const rect = svgRef.current?.getBoundingClientRect()
    if (!rect) {
      return
    }
    const box = contentBox(rect)
    if (!box.side) {
      return
    }
    const dx = ((event.clientX - drag.startX) / box.side) * VIEW * 2
    const dy = ((event.clientY - drag.startY) / box.side) * VIEW * 2
    applyView(scaleRef.current, { x: drag.originX + dx, y: drag.originY + dy })
  }

  const onPointerUp = (event: PointerEvent<SVGSVGElement>) => {
    if (dragRef.current?.pointerId === event.pointerId) {
      dragRef.current = null
      event.currentTarget.releasePointerCapture(event.pointerId)
    }
  }

  const reset = () => applyView(1, { x: 0, y: 0 })

  const unit =
    viewport.width > 0 && viewport.height > 0
      ? (VIEW * 2) / Math.min(viewport.width, viewport.height)
      : 0
  const baseRadius = Math.min(7, Math.max(2.8, 16 / Math.sqrt(points.length + 1)))
  const pointRadius = unit > 0 ? baseRadius * unit : 0.024
  const ringRadius = pointRadius + (unit > 0 ? 3.2 * unit : 0.014)
  const selectedRadius = pointRadius + (unit > 0 ? 2 * unit : 0.01)
  // At least ~16 CSS pixels of hit target, even when a point is only 3px wide.
  const hitRadius = Math.max(pointRadius * 1.8, unit > 0 ? 8 * unit : 0.05)

  const focusPoint = (index: number) => {
    const target = svgRef.current?.querySelector<SVGGElement>(`[data-point-index="${index}"]`)
    target?.focus()
    setFocusIndex(index)
  }

  const onPointKeyDown = (event: KeyboardEvent<SVGGElement>, index: number) => {
    const point = points[index]
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault()
      onSelect(selectedId === point.chunk_id ? null : point.chunk_id)
      return
    }
    const step = event.key === 'ArrowRight' || event.key === 'ArrowDown' ? 1 : event.key === 'ArrowLeft' || event.key === 'ArrowUp' ? -1 : 0
    if (step !== 0 && points.length > 0) {
      event.preventDefault()
      focusPoint((index + step + points.length) % points.length)
    }
  }

  return (
    <div className="plot2d">
      <svg
        ref={svgRef}
        viewBox={`${-VIEW} ${-VIEW} ${VIEW * 2} ${VIEW * 2}`}
        className="plot2d__svg"
        role="group"
        aria-label="Two-dimensional PCA projection of document embeddings, coloured by document. Use the arrow keys to move between points and Enter to select one."
        aria-describedby={describedBy}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
        onDoubleClick={reset}
      >
        <rect x={-VIEW} y={-VIEW} width={VIEW * 2} height={VIEW * 2} fill="transparent" />
        <g transform={`translate(${offset.x} ${offset.y}) scale(${scale})`}>
          <g className="plot-grid">
            {[-1, -0.5, 0.5, 1].map((value) => (
              <g key={value}>
                <line x1={value} y1={-1} x2={value} y2={1} />
                <line x1={-1} y1={value} x2={1} y2={value} />
              </g>
            ))}
            <line className="plot-axis" x1={0} y1={-1} x2={0} y2={1} />
            <line className="plot-axis" x1={-1} y1={0} x2={1} y2={0} />
          </g>
          <text className="plot-label" x={0} y={-1.04} textAnchor="middle">
            PC2
          </text>
          <text className="plot-label" x={1.04} y={0} textAnchor="start" dominantBaseline="middle">
            PC1
          </text>

          {points.map((point, index) => {
            const coordinate = coordinates[index]
            if (!coordinate) {
              return null
            }
            const selected = selectedId === point.chunk_id
            const hovered = hoveredId === point.chunk_id
            const isHighlighted = showQuery && highlighted.has(point.chunk_id)
            const dimmed = focusedDocument !== null && point.document_id !== focusedDocument

            return (
              <g
                key={point.chunk_id}
                data-point-index={index}
                transform={`translate(${coordinate[0]} ${coordinate[1]})`}
                opacity={dimmed ? 0.16 : 1}
                className="plot-point"
                role="button"
                tabIndex={index === focusIndex ? 0 : -1}
                aria-label={`${point.title}, chunk ${point.chunk_id}${isHighlighted ? ', retrieved for the last question' : ''}`}
                aria-pressed={selected}
                onFocus={() => {
                  setFocusIndex(index)
                  onHover(point.chunk_id)
                }}
                onBlur={() => onHover(null)}
                onKeyDown={(event) => onPointKeyDown(event, index)}
              >
                {isHighlighted ? (
                  <circle className="plot-point__hit" r={ringRadius} stroke="var(--query)" />
                ) : null}
                {selected ? <circle className="plot-point__selected" r={selectedRadius} /> : null}
                <circle
                  r={hovered ? pointRadius * 1.35 : pointRadius}
                  fill={colorFor(point.document_id)}
                  className="plot-point__dot"
                />
                {/* Transparent target layered above the dot so small points stay clickable. */}
                <circle
                  r={hitRadius}
                  fill="transparent"
                  className="plot-point__target"
                  onPointerDown={(event) => event.stopPropagation()}
                  onClick={(event) => {
                    event.stopPropagation()
                    onSelect(selected ? null : point.chunk_id)
                  }}
                  onDoubleClick={reset}
                  onMouseEnter={() => onHover(point.chunk_id)}
                  onMouseLeave={() => onHover(null)}
                >
                  <title>
                    {point.title} · chunk {point.chunk_id} · {truncate(point.preview, 120)}
                  </title>
                </circle>
              </g>
            )
          })}

          {showQuery && query ? (
            <g transform={`translate(${query[0]} ${query[1]})`} className="plot-query">
              <circle r={pointRadius * 3.2} className="plot-query__halo" />
              <polygon
                points={`0,${-pointRadius * 2} ${pointRadius * 2},0 0,${pointRadius * 2} ${-pointRadius * 2},0`}
                fill="var(--query)"
              />
              <text y={-pointRadius * 3.4} textAnchor="middle">
                query
              </text>
            </g>
          ) : null}
        </g>
      </svg>

      {points.length === 0 ? <p className="plot2d__empty">No points to plot yet.</p> : null}

      <div className="plot-controls">
        <button
          type="button"
          onClick={() => applyView(Math.min(4, scaleRef.current * 1.2), offsetRef.current)}
          aria-label="Zoom in"
        >
          +
        </button>
        <button
          type="button"
          onClick={() => applyView(Math.max(0.7, scaleRef.current / 1.2), offsetRef.current)}
          aria-label="Zoom out"
        >
          −
        </button>
        <button type="button" onClick={reset} aria-label="Reset map view">
          ↺
        </button>
      </div>
      <span className="plot-hint">Drag to pan · scroll to zoom · double-click to reset</span>
    </div>
  )
}
