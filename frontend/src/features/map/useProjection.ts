import { useEffect, useRef, useState } from 'react'
import type { MapPoint } from '../../api/types'
import { projectMap, type Projection } from '../../lib/pca'

interface ProjectionState {
  projection: Projection | null
  projecting: boolean
}

export function useProjection(points: MapPoint[], query: number[] | null): ProjectionState {
  const [projection, setProjection] = useState<Projection | null>(null)
  const [projecting, setProjecting] = useState(false)
  const requestId = useRef(0)

  useEffect(() => {
    if (points.length === 0) {
      setProjection(null)
      setProjecting(false)
      return
    }

    const vectors = points.map((point) => point.vector)
    requestId.current += 1
    const id = requestId.current

    if (typeof Worker === 'undefined') {
      setProjection(projectMap(vectors, query))
      setProjecting(false)
      return
    }

    setProjecting(true)
    const worker = new Worker(new URL('../../lib/pca.worker.ts', import.meta.url), {
      type: 'module',
    })
    worker.onmessage = (event: MessageEvent<{ id: number; projection: Projection | null }>) => {
      if (event.data.id === id) {
        setProjection(event.data.projection)
        setProjecting(false)
      }
    }
    worker.onerror = () => {
      if (requestId.current === id) {
        setProjection(projectMap(vectors, query))
        setProjecting(false)
      }
    }
    worker.postMessage({ id, vectors, query })
    return () => worker.terminate()
  }, [points, query])

  return { projection, projecting }
}
