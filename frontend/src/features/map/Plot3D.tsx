import { useEffect, useRef, useState, type PointerEvent } from 'react'
import type { MapPoint } from '../../api/types'
import { useTheme } from '../../app/ThemeProvider'
import { truncate } from '../../lib/format'

interface Plot3DProps {
  points: MapPoint[]
  coordinates: number[][]
  query: number[] | null
  colorFor: (documentId: number) => string
  highlighted: Set<number>
  selectedId: number | null
  hoveredId: number | null
  focusedDocument: number | null
  showQuery: boolean
  onSelect: (id: number | null) => void
  onHover: (id: number | null) => void
}

interface ProjectedPoint {
  id: number
  x: number
  y: number
  depth: number
}

const FOCAL = 2.65

export function Plot3D({
  points,
  coordinates,
  query,
  colorFor,
  highlighted,
  selectedId,
  hoveredId,
  focusedDocument,
  showQuery,
  onSelect,
  onHover,
}: Plot3DProps) {
  const frameRef = useRef<HTMLDivElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const projectedRef = useRef<ProjectedPoint[]>([])
  const hoverRef = useRef<number | null>(null)
  const pointerRef = useRef<{
    pointerId: number
    startX: number
    startY: number
    lastX: number
    lastY: number
    moved: boolean
    orbit: boolean
  } | null>(null)
  const [size, setSize] = useState({ width: 0, height: 0 })
  const [yaw, setYaw] = useState(0.62)
  const [pitch, setPitch] = useState(-0.3)
  const [distance, setDistance] = useState(3.2)
  const { theme } = useTheme()

  useEffect(() => {
    const frame = frameRef.current
    if (!frame) {
      return
    }
    const observer = new ResizeObserver((entries) => {
      const rect = entries[0]?.contentRect
      if (rect) {
        setSize({ width: rect.width, height: rect.height })
      }
    })
    observer.observe(frame)
    return () => observer.disconnect()
  }, [])

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) {
      return
    }
    const onWheel = (event: WheelEvent) => {
      event.preventDefault()
      setDistance((current) => Math.min(6, Math.max(1.9, current * (event.deltaY > 0 ? 1.1 : 0.9))))
    }
    canvas.addEventListener('wheel', onWheel, { passive: false })
    return () => canvas.removeEventListener('wheel', onWheel)
  }, [])

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas || size.width === 0 || size.height === 0) {
      return
    }
    const dpr = Math.min(window.devicePixelRatio || 1, 2)
    const width = Math.round(size.width)
    const height = Math.round(size.height)
    if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
      canvas.width = Math.round(width * dpr)
      canvas.height = Math.round(height * dpr)
    }
    const context = canvas.getContext('2d')
    if (!context) {
      return
    }
    context.setTransform(dpr, 0, 0, dpr, 0, 0)
    context.clearRect(0, 0, width, height)

    const styles = getComputedStyle(document.documentElement)
    const ink = styles.getPropertyValue('--ink-5').trim() || '#77807a'
    const grid = styles.getPropertyValue('--map-grid').trim() || '#d9ded8'
    const queryColor = styles.getPropertyValue('--query').trim() || '#f0b65e'
    const centerX = width / 2
    const centerY = height / 2
    const unit = Math.min(width, height) * 0.39
    const cosYaw = Math.cos(yaw)
    const sinYaw = Math.sin(yaw)
    const cosPitch = Math.cos(pitch)
    const sinPitch = Math.sin(pitch)

    const project = (x: number, y: number, z: number) => {
      const rx = x * cosYaw - z * sinYaw
      const rz = x * sinYaw + z * cosYaw
      const ry = y * cosPitch - rz * sinPitch
      const depth = Math.max(0.45, distance + (y * sinPitch + rz * cosPitch))
      const perspective = FOCAL / depth
      return {
        x: centerX + rx * unit * perspective,
        y: centerY - ry * unit * perspective,
        depth,
      }
    }

    context.strokeStyle = grid
    context.lineWidth = 0.9
    context.globalAlpha = 0.75
    for (let step = -1; step <= 1.001; step += 0.5) {
      const a = project(step, -1.15, -1.15)
      const b = project(step, -1.15, 1.15)
      const c = project(-1.15, -1.15, step)
      const d = project(1.15, -1.15, step)
      context.beginPath()
      context.moveTo(a.x, a.y)
      context.lineTo(b.x, b.y)
      context.moveTo(c.x, c.y)
      context.lineTo(d.x, d.y)
      context.stroke()
    }
    context.globalAlpha = 1

    const axis = (x: number, y: number, z: number, label: string) => {
      const end = project(x, y, z)
      context.strokeStyle = ink
      context.globalAlpha = 0.68
      context.lineWidth = 1.1
      context.beginPath()
      context.moveTo(centerX, centerY)
      context.lineTo(end.x, end.y)
      context.stroke()
      context.globalAlpha = 0.95
      context.fillStyle = ink
      context.font = '600 10px "JetBrains Mono Variable", monospace'
      context.fillText(label, end.x + 5, end.y - 3)
      context.globalAlpha = 1
    }
    axis(1.4, 0, 0, 'PC1')
    axis(0, 1.4, 0, 'PC2')
    axis(0, 0, 1.4, 'PC3')

    const byId = new Map(points.map((point) => [point.chunk_id, point]))
    const projected: ProjectedPoint[] = []
    points.forEach((point, index) => {
      const coordinate = coordinates[index]
      if (!coordinate) {
        return
      }
      const position = project(coordinate[0] ?? 0, coordinate[1] ?? 0, coordinate[2] ?? 0)
      projected.push({ id: point.chunk_id, x: position.x, y: position.y, depth: position.depth })
    })
    projected.sort((a, b) => b.depth - a.depth)
    projectedRef.current = projected

    const baseRadius = Math.min(6.4, Math.max(2.6, 16 / Math.sqrt(points.length + 1)))

    for (const position of projected) {
      const point = byId.get(position.id)
      if (!point) {
        continue
      }
      const dimmed = focusedDocument !== null && point.document_id !== focusedDocument
      const isHovered = hoveredId === point.chunk_id
      const isSelected = selectedId === point.chunk_id
      const isHighlighted = showQuery && highlighted.has(point.chunk_id)
      const radius = baseRadius * Math.min(1.3, Math.max(0.82, 3.2 / position.depth))

      context.globalAlpha = dimmed ? 0.14 : 0.92
      context.fillStyle = colorFor(point.document_id)
      context.beginPath()
      context.arc(position.x, position.y, isHovered ? radius + 1.3 : radius, 0, Math.PI * 2)
      context.fill()

      if (isHighlighted) {
        context.globalAlpha = 1
        context.strokeStyle = queryColor
        context.lineWidth = 2.2
        context.beginPath()
        context.arc(position.x, position.y, radius + 5, 0, Math.PI * 2)
        context.stroke()
      }
      if (isSelected || isHovered) {
        context.globalAlpha = 1
        context.strokeStyle = ink
        context.lineWidth = isSelected ? 1.9 : 1.2
        context.beginPath()
        context.arc(position.x, position.y, radius + 3, 0, Math.PI * 2)
        context.stroke()
      }
    }
    context.globalAlpha = 1

    if (showQuery && query && query.length >= 3) {
      const position = project(query[0] ?? 0, query[1] ?? 0, query[2] ?? 0)
      context.save()
      context.translate(position.x, position.y)
      context.strokeStyle = queryColor
      context.globalAlpha = 0.55
      context.setLineDash([3, 3])
      context.beginPath()
      context.arc(0, 0, 10, 0, Math.PI * 2)
      context.stroke()
      context.setLineDash([])
      context.rotate(Math.PI / 4)
      context.fillStyle = queryColor
      context.fillRect(-5, -5, 10, 10)
      context.restore()
      context.globalAlpha = 1
      context.fillStyle = queryColor
      context.font = '600 10px "Inter Variable", sans-serif'
      context.fillText('query', position.x + 13, position.y - 7)
    }

    const selected = points.find((point) => point.chunk_id === selectedId)
    const selectedPosition = selected
      ? projected.find((position) => position.id === selected.chunk_id)
      : undefined
    if (selected && selectedPosition) {
      const label = truncate(selected.preview || selected.title, 38)
      context.font = '500 11px "Inter Variable", sans-serif'
      const textWidth = context.measureText(label).width
      const x = Math.min(width - textWidth - 18, selectedPosition.x + 10)
      const y = Math.max(18, Math.min(height - 10, selectedPosition.y - 10))
      context.fillStyle = 'rgba(18, 17, 15, 0.88)'
      context.beginPath()
      context.roundRect(x - 7, y - 13, textWidth + 14, 20, 5)
      context.fill()
      context.fillStyle = '#f5f2eb'
      context.fillText(label, x, y + 1)
    }
  }, [
    size,
    points,
    coordinates,
    query,
    colorFor,
    highlighted,
    selectedId,
    hoveredId,
    focusedDocument,
    showQuery,
    yaw,
    pitch,
    distance,
    theme,
  ])

  const zoom = (next: number) => setDistance(Math.min(6, Math.max(1.9, next)))

  const reset = () => {
    setYaw(0.62)
    setPitch(-0.3)
    setDistance(3.2)
  }

  const findNearest = (clientX: number, clientY: number) => {
    const rect = canvasRef.current?.getBoundingClientRect()
    if (!rect) {
      return null
    }
    const x = clientX - rect.left
    const y = clientY - rect.top
    let best: ProjectedPoint | null = null
    let bestDistance = 12
    for (const position of projectedRef.current) {
      const distanceSquared = (position.x - x) ** 2 + (position.y - y) ** 2
      if (distanceSquared < bestDistance ** 2) {
        best = position
        bestDistance = Math.sqrt(distanceSquared)
      }
    }
    return best
  }

  const onPointerDown = (event: PointerEvent<HTMLCanvasElement>) => {
    if (event.button !== 0) {
      return
    }
    pointerRef.current = {
      pointerId: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      lastX: event.clientX,
      lastY: event.clientY,
      moved: false,
      orbit: !findNearest(event.clientX, event.clientY),
    }
    event.currentTarget.setPointerCapture(event.pointerId)
  }

  const onPointerMove = (event: PointerEvent<HTMLCanvasElement>) => {
    const pointer = pointerRef.current
    if (!pointer || pointer.pointerId !== event.pointerId) {
      const nearest = findNearest(event.clientX, event.clientY)
      const id = nearest?.id ?? null
      if (id !== hoverRef.current) {
        hoverRef.current = id
        onHover(id)
      }
      return
    }
    const dx = event.clientX - pointer.lastX
    const dy = event.clientY - pointer.lastY
    if (Math.abs(event.clientX - pointer.startX) + Math.abs(event.clientY - pointer.startY) > 4) {
      pointer.moved = true
    }
    pointer.lastX = event.clientX
    pointer.lastY = event.clientY
    if (pointer.orbit) {
      if (pointer.moved) {
        setYaw((value) => value + dx * 0.007)
        setPitch((value) => Math.max(-1.35, Math.min(1.35, value - dy * 0.007)))
      }
      return
    }
    const nearest = findNearest(event.clientX, event.clientY)
    const id = nearest?.id ?? null
    if (id !== hoverRef.current) {
      hoverRef.current = id
      onHover(id)
    }
  }

  const onPointerUp = (event: PointerEvent<HTMLCanvasElement>) => {
    const pointer = pointerRef.current
    if (!pointer || pointer.pointerId !== event.pointerId) {
      return
    }
    if (!pointer.moved) {
      const nearest = findNearest(event.clientX, event.clientY)
      onSelect(nearest ? (nearest.id === selectedId ? null : nearest.id) : null)
    }
    pointerRef.current = null
    event.currentTarget.releasePointerCapture(event.pointerId)
  }

  return (
    <div className="plot3d">
      <div ref={frameRef} className="plot3d__frame">
        <canvas
          ref={canvasRef}
          className="plot3d__canvas"
          role="img"
          aria-label="Three-dimensional PCA projection of document embeddings. Drag to orbit, scroll to zoom, and select a point to inspect it."
          onPointerDown={onPointerDown}
          onPointerMove={onPointerMove}
          onPointerUp={onPointerUp}
          onPointerCancel={() => {
            pointerRef.current = null
          }}
          onPointerLeave={() => {
            hoverRef.current = null
            onHover(null)
          }}
        />
      </div>
      <div className="plot-controls">
        <button type="button" onClick={() => zoom(distance * 0.86)} aria-label="Zoom in">
          +
        </button>
        <button type="button" onClick={() => zoom(distance * 1.16)} aria-label="Zoom out">
          −
        </button>
        <button type="button" onClick={reset} aria-label="Reset 3D view">
          ↺
        </button>
      </div>
      <span className="plot-hint">Drag to orbit · scroll to zoom · click a point to inspect</span>
    </div>
  )
}
