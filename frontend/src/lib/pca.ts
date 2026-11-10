export interface PcaModel {
  mean: number[]
  components: number[][]
  /** Fit vectors projected onto the components, in the same order as the input. */
  coordinates: number[][]
}

export interface Projection {
  points: number[][]
  query: number[] | null
  components: number[][]
  mean: number[]
}

function dot(a: number[], b: number[]): number {
  let total = 0
  for (let i = 0; i < a.length; i += 1) {
    total += a[i] * b[i]
  }
  return total
}

function norm(value: number[]): number {
  return Math.sqrt(dot(value, value))
}

/**
 * PCA via NIPALS power iteration. This avoids materializing a 256 x 256
 * covariance matrix for every map request and keeps the projection fast enough
 * to run in the browser.
 */
export function fitPca(vectors: number[][], componentCount = 3): PcaModel | null {
  if (vectors.length === 0) {
    return null
  }
  const dim = vectors[0].length
  if (dim === 0 || vectors.some((vector) => vector.length !== dim)) {
    return null
  }

  const mean = new Array<number>(dim).fill(0)
  for (const vector of vectors) {
    for (let d = 0; d < dim; d += 1) {
      mean[d] += vector[d] / vectors.length
    }
  }

  let residual = vectors.map((vector) => vector.map((value, d) => value - mean[d]))
  const components: number[][] = []
  const wanted = Math.min(componentCount, vectors.length, dim)

  for (let component = 0; component < wanted; component += 1) {
    // A deterministic starting vector keeps the map stable between renders.
    let direction = new Array<number>(dim).fill(0)
    direction[component % dim] = 1

    for (let iteration = 0; iteration < 80; iteration += 1) {
      const scores = residual.map((row) => dot(row, direction))
      const scoreEnergy = scores.reduce((total, score) => total + score * score, 0)
      if (scoreEnergy < 1e-12) {
        break
      }
      const next = new Array<number>(dim).fill(0)
      for (let i = 0; i < residual.length; i += 1) {
        const score = scores[i]
        const row = residual[i]
        for (let d = 0; d < dim; d += 1) {
          next[d] += score * row[d]
        }
      }
      const length = norm(next)
      if (length < 1e-12) {
        break
      }
      direction = next.map((value) => value / length)
    }

    let length = norm(direction)
    if (length < 1e-12) {
      direction = new Array<number>(dim).fill(0)
      direction[component % dim] = 1
      length = 1
    }
    direction = direction.map((value) => value / length)

    // Keep the sign deterministic so points do not jump when re-projecting.
    let largest = 0
    for (let d = 1; d < dim; d += 1) {
      if (Math.abs(direction[d]) > Math.abs(direction[largest])) {
        largest = d
      }
    }
    if (direction[largest] < 0) {
      direction = direction.map((value) => -value)
    }

    const scores = residual.map((row) => dot(row, direction))
    components.push(direction)
    residual = residual.map((row, index) => row.map((value, d) => value - scores[index] * direction[d]))
  }

  const coordinates = vectors.map((vector) => {
    const centered = vector.map((value, d) => value - mean[d])
    return components.map((component) => dot(centered, component))
  })

  return { mean, components, coordinates }
}

function normalize(values: number[][]): { points: number[][]; ranges: Array<[number, number]> } {
  if (values.length === 0) {
    return { points: [], ranges: [] }
  }
  const dimensions = values[0].length
  const ranges: Array<[number, number]> = []
  for (let d = 0; d < dimensions; d += 1) {
    let min = Infinity
    let max = -Infinity
    for (const value of values) {
      min = Math.min(min, value[d])
      max = Math.max(max, value[d])
    }
    if (!Number.isFinite(min) || !Number.isFinite(max) || Math.abs(max - min) < 1e-12) {
      ranges.push([-1, 1])
    } else {
      const padding = (max - min) * 0.08
      ranges.push([min - padding, max + padding])
    }
  }

  const points = values.map((value) =>
    value.map((component, d) => {
      const [min, max] = ranges[d]
      return ((component - min) / (max - min)) * 2 - 1
    }),
  )
  return { points, ranges }
}

/**
 * Fits the projection on the map points, then projects the optional query with
 * the same basis. The query is included in the normalization bounds so it stays
 * visible, without changing the principal directions of the corpus.
 */
export function projectMap(vectors: number[][], query?: number[] | null): Projection | null {
  const model = fitPca(vectors, 3)
  if (!model) {
    return null
  }

  const queryCoordinates =
    query && query.length === model.mean.length
      ? model.components.map((component) => dot(query.map((value, d) => value - model.mean[d]), component))
      : null

  const all = queryCoordinates ? [...model.coordinates, queryCoordinates] : model.coordinates
  const normalized = normalize(all)
  return {
    points: normalized.points.slice(0, vectors.length),
    query: queryCoordinates ? normalized.points[normalized.points.length - 1] : null,
    components: model.components,
    mean: model.mean,
  }
}
