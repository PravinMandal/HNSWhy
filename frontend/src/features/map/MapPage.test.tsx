import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { DataProvider } from '../../app/DataProvider'
import { MapPage } from './MapPage'

const apiMocks = vi.hoisted(() => ({
  map: vi.fn(),
  status: vi.fn(),
  health: vi.fn(),
  documents: vi.fn(),
}))

vi.mock('../../api/client', () => ({ api: apiMocks }))

const mapResponse = {
  total_chunks: 2,
  sampled: 2,
  points: [
    {
      chunk_id: 1,
      document_id: 10,
      title: 'Graph notes',
      ordinal: 0,
      preview: 'HNSW builds a layered graph.',
      vector: [1, 0, 0],
    },
    {
      chunk_id: 2,
      document_id: 20,
      title: 'Search notes',
      ordinal: 0,
      preview: 'Search descends the layers.',
      vector: [0, 1, 0],
    },
  ],
}

function renderMap() {
  return render(
    <DataProvider>
      <MapPage onOpenDocuments={vi.fn()} />
    </DataProvider>,
  )
}

describe('MapPage', () => {
  beforeEach(() => {
    vi.stubGlobal('Worker', undefined)
    apiMocks.map.mockReset().mockResolvedValue(mapResponse)
    apiMocks.status.mockReset().mockResolvedValue({
      database: 'hnswhy.db',
      store: { documents: 2, chunks: 2, embeddings: 2 },
      index: { live: 2, slots: 2, tombstones: 0, dim: 3 },
      embedding: { provider: 'hash', model: 'local-hash-v1', dim: 3 },
      chat: { provider: 'extractive', model: 'local-extractive-v1' },
      query_ef: 50,
      sample_loaded: false,
      benchmark_available: false,
    })
    apiMocks.health.mockReset().mockResolvedValue({
      status: 'ok',
      database: 'ok',
      store: { documents: 2, chunks: 2, embeddings: 2 },
      embedding: { provider: 'hash', model: 'local-hash-v1', available: true },
      chat: { provider: 'extractive', model: 'local-extractive-v1', available: true },
    })
    apiMocks.documents.mockReset().mockResolvedValue([])
    sessionStorage.clear()
  })

  it('plots the sample and lets a keyboard walk between points', async () => {
    renderMap()

    const plot = await screen.findByRole('group', { name: /Two-dimensional PCA/ })
    const first = screen.getByRole('button', { name: /Graph notes, chunk 1/ })
    const second = screen.getByRole('button', { name: /Search notes, chunk 2/ })
    expect(plot).toBeInTheDocument()
    expect(first).toHaveAttribute('tabindex', '0')
    expect(second).toHaveAttribute('tabindex', '-1')

    first.focus()
    fireEvent.keyDown(first, { key: 'ArrowRight' })
    expect(second).toHaveFocus()

    fireEvent.keyDown(second, { key: 'Enter' })
    expect(await screen.findByRole('heading', { name: 'Search notes' })).toBeInTheDocument()
    expect(screen.getByText('not retrieved')).toBeInTheDocument()
  })

  it('disables the query overlay until a question has been asked', async () => {
    renderMap()

    await screen.findByRole('group', { name: /Two-dimensional PCA/ })
    expect(screen.getByRole('button', { name: /Query overlay/ })).toBeDisabled()
  })

  it('explains when the saved query cannot be projected', async () => {
    sessionStorage.setItem(
      'hnswhy-last-answer',
      JSON.stringify({
        question: 'Which chunk?',
        answer: { answer: 'Answer [1]', sources: [], query_vector: [1, 2, 3, 4], timings_ms: {}, models: {} },
      }),
    )
    renderMap()

    await screen.findByRole('group', { name: /Two-dimensional PCA/ })
    await waitFor(() =>
      expect(screen.getByRole('button', { name: /Query overlay/ })).toBeDisabled(),
    )
    expect(
      screen.getByText(/different dimension than this map/),
    ).toBeInTheDocument()
  })
})
