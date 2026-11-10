import { describe, expect, it } from 'vitest'
import { fitPca, projectMap } from './pca'

describe('fitPca', () => {
  it('rejects empty, ragged, and zero-dimensional input', () => {
    expect(fitPca([])).toBeNull()
    expect(fitPca([[1, 2], [3]])).toBeNull()
    expect(fitPca([[], []])).toBeNull()
  })

  it('finds the dominant direction of a line', () => {
    const model = fitPca([[0, 0], [1, 0], [2, 0], [3, 0]], 2)
    expect(model).not.toBeNull()
    const first = model?.components[0] ?? []
    expect(Math.abs(first[0] ?? 0)).toBeGreaterThan(0.999)
    expect(Math.abs(first[1] ?? 0)).toBeLessThan(0.001)
  })

  it('centres coordinates on the mean', () => {
    const model = fitPca([[1, 1], [3, 1], [5, 1]], 1)
    const scores = model?.coordinates.map((coordinate) => coordinate[0]) ?? []
    expect(scores.reduce((total, value) => total + value, 0)).toBeCloseTo(0, 10)
  })

  it('is deterministic for the same input', () => {
    const vectors = [[0.1, 0.4, 0.2], [0.8, 0.1, 0.6], [0.3, 0.9, 0.5], [0.7, 0.7, 0.1]]
    expect(fitPca(vectors)).toEqual(fitPca(vectors))
  })

  it('caps components at the number of points and dimensions', () => {
    expect(fitPca([[1, 2], [3, 4]], 3)?.components).toHaveLength(2)
    expect(fitPca([[1], [2], [3]], 3)?.components).toHaveLength(1)
  })
})

describe('projectMap', () => {
  it('normalizes corpus points into the viewport', () => {
    const projection = projectMap([[0, 0], [1, 2], [2, 4]])
    expect(projection).not.toBeNull()
    for (const point of projection?.points ?? []) {
      for (const value of point) {
        expect(value).toBeGreaterThanOrEqual(-1)
        expect(value).toBeLessThanOrEqual(1)
      }
    }
  })

  it('projects a compatible query with the corpus basis', () => {
    const projection = projectMap([[0, 0], [1, 0], [2, 0]], [1.5, 0])
    expect(projection?.query).not.toBeNull()
    const query = projection?.query ?? []
    const points = projection?.points ?? []
    const sorted = points.map((point) => point[0]).sort((a, b) => a - b)
    expect(query[0]).toBeGreaterThan(sorted[1] ?? 0)
    expect(query[0]).toBeLessThanOrEqual(1)
  })

  it('ignores a query with the wrong dimension', () => {
    const projection = projectMap([[0, 0], [1, 1]], [1, 2, 3])
    expect(projection).not.toBeNull()
    expect(projection?.query).toBeNull()
  })
})
