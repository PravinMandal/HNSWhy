import { render, screen, waitFor } from '@testing-library/react'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { api } from '../api/client'
import type { DocumentSummary, HealthResponse, StatusResponse } from '../api/types'
import { DataProvider, useData } from './DataProvider'

vi.mock('../api/client', () => ({
  api: {
    status: vi.fn(),
    health: vi.fn(),
    documents: vi.fn(),
  },
}))

const status: StatusResponse = {
  database: 'hnswhy.db',
  store: { documents: 1, chunks: 1, embeddings: 1 },
  index: { live: 1, slots: 1, tombstones: 0, dim: 256 },
  embedding: { provider: 'hash', model: 'local-hash-v1', dim: 256 },
  chat: { provider: 'extractive', model: 'local-extractive-v1' },
  query_ef: 50,
  sample_loaded: true,
  benchmark_available: true,
}

const health: HealthResponse = {
  status: 'ok',
  database: 'hnswhy.db',
  store: { documents: 1, chunks: 1, embeddings: 1 },
  embedding: { provider: 'hash', model: 'local-hash-v1', available: true },
  chat: { provider: 'extractive', model: 'local-extractive-v1', available: true },
}

const documents: DocumentSummary[] = [
  {
    id: 1,
    title: 'HNSW notes',
    source: 'notes.md',
    chunk_count: 1,
    char_count: 80,
    created_at: '2026-11-10T14:04:58Z',
  },
]

function Probe() {
  const { documents: items, loading, error, refreshAll } = useData()
  return (
    <div>
      <span data-testid="loading">{String(loading)}</span>
      <span data-testid="error">{error ?? ''}</span>
      <span data-testid="documents">{items.map((item) => item.title).join(',')}</span>
      <button type="button" onClick={() => void refreshAll()}>
        Retry
      </button>
    </div>
  )
}

describe('DataProvider', () => {
  beforeEach(() => {
    vi.mocked(api.status).mockResolvedValue(status)
    vi.mocked(api.health).mockResolvedValue(health)
    vi.mocked(api.documents).mockResolvedValue(documents)
  })

  it('loads status, health and documents together', async () => {
    render(
      <DataProvider>
        <Probe />
      </DataProvider>,
    )

    await waitFor(() => expect(screen.getByTestId('loading')).toHaveTextContent('false'))
    expect(screen.getByTestId('documents')).toHaveTextContent('HNSW notes')
    expect(screen.getByTestId('error')).toHaveTextContent('')
  })

  it('surfaces a load failure and lets the user retry', async () => {
    vi.mocked(api.documents).mockRejectedValueOnce(new Error('database is locked'))

    render(
      <DataProvider>
        <Probe />
      </DataProvider>,
    )

    await waitFor(() => expect(screen.getByTestId('error')).toHaveTextContent('database is locked'))
    expect(screen.getByTestId('documents')).toHaveTextContent('')

    screen.getByRole('button', { name: 'Retry' }).click()

    await waitFor(() => expect(screen.getByTestId('documents')).toHaveTextContent('HNSW notes'))
    expect(screen.getByTestId('error')).toHaveTextContent('')
  })
})
