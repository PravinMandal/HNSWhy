import { render, screen, waitFor } from '@testing-library/react'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { DataProvider } from '../../app/DataProvider'
import { ToastProvider } from '../../app/ToastProvider'
import { DocumentsPage } from './DocumentsPage'

const apiMocks = vi.hoisted(() => ({
  documents: vi.fn(),
  status: vi.fn(),
  health: vi.fn(),
  deleteDocument: vi.fn(),
  document: vi.fn(),
  upload: vi.fn(),
  addText: vi.fn(),
}))

vi.mock('../../api/client', () => ({ api: apiMocks }))

function renderPage() {
  return render(
    <ToastProvider>
      <DataProvider>
        <DocumentsPage />
      </DataProvider>
    </ToastProvider>,
  )
}

describe('DocumentsPage', () => {
  beforeEach(() => {
    apiMocks.documents.mockReset()
    apiMocks.status.mockReset().mockResolvedValue({
      database: 'hnswhy.db',
      store: { documents: 0, chunks: 0, embeddings: 0 },
      index: { live: 0, slots: 0, tombstones: 0, dim: 0 },
      embedding: { provider: 'hash', model: 'local-hash-v1', dim: 256 },
      chat: { provider: 'extractive', model: 'local-extractive-v1' },
      query_ef: 50,
      sample_loaded: false,
      benchmark_available: false,
    })
    apiMocks.health.mockReset().mockResolvedValue({
      status: 'ok',
      database: 'ok',
      store: { documents: 0, chunks: 0, embeddings: 0 },
      embedding: { provider: 'hash', model: 'local-hash-v1', available: true },
      chat: { provider: 'extractive', model: 'local-extractive-v1', available: true },
    })
  })

  it('shows the load error instead of a false empty-library prompt', async () => {
    // With no status payload, counts are unknown and must render as em dashes.
    apiMocks.status.mockRejectedValue(new Error('The server is not reachable.'))
    apiMocks.documents.mockRejectedValue(new Error('The server is not reachable.'))
    renderPage()

    expect(await screen.findByText('The server is not reachable.')).toBeInTheDocument()
    expect(screen.queryByText('Build your first knowledge base')).not.toBeInTheDocument()
    // Unknown server counts must not masquerade as measured zeros.
    expect(screen.getAllByText('—').length).toBeGreaterThanOrEqual(3)
  })

  it('shows the empty-library prompt when the request succeeds with no documents', async () => {
    apiMocks.documents.mockResolvedValue([])
    renderPage()

    expect(await screen.findByText('Build your first knowledge base')).toBeInTheDocument()
    await waitFor(() => expect(screen.queryByText('The server is not reachable.')).not.toBeInTheDocument())
  })
})
