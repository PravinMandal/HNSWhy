/// <reference lib="webworker" />

import { projectMap } from './pca'

interface ProjectRequest {
  id: number
  vectors: number[][]
  query: number[] | null
}

const worker = self as unknown as DedicatedWorkerGlobalScope

worker.onmessage = (event: MessageEvent<ProjectRequest>) => {
  const { id, vectors, query } = event.data
  const projection = projectMap(vectors, query)
  worker.postMessage({ id, projection })
}
