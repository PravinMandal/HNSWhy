import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { DataProvider } from '../../app/DataProvider'
import { ToastProvider } from '../../app/ToastProvider'
import { ToastViewport } from '../../ui/ToastViewport'
import { AskPage } from './AskPage'

const apiMocks = vi.hoisted(() => ({
  ask: vi.fn(),
  status: vi.fn(),
  health: vi.fn(),
  documents: vi.fn(),
}))

vi.mock('../../api/client', () => ({ api: apiMocks }))

const sampleDocument = {
  id: 1,
  title: 'HNSW handbook',
  source: 'sample',
  chunk_count: 3,
  char_count: 900,
  created_at: '2026-11-10T14:04:58Z',
}

const storedAnswer = {
  answer: {
    answer: 'A layered graph [1].',
    sources: [
      {
        chunk_id: 1,
        document_id: 1,
        title: 'HNSW handbook',
        ordinal: 0,
        text: 'HNSW builds a layered graph.',
        distance: 0.2,
      },
    ],
    query_vector: [1, 0],
    timings_ms: { embedding: 1, search: 1, generate: 1, total: 3 },
    models: {
      embedding_provider: 'hash',
      embedding_model: 'local-hash-v1',
      chat_provider: 'extractive',
      chat_model: 'local-extractive-v1',
    },
  },
  question: 'How does HNSW work?',
}

function renderAsk() {
  return render(
    <ToastProvider>
      <DataProvider>
        <AskPage onOpenMap={vi.fn()} onOpenDocuments={vi.fn()} />
      </DataProvider>
      <ToastViewport />
    </ToastProvider>,
  )
}

describe('AskPage', () => {
  beforeEach(() => {
    apiMocks.ask.mockReset()
    apiMocks.status.mockReset().mockResolvedValue({
      database: 'hnswhy.db',
      store: { documents: 1, chunks: 3, embeddings: 3 },
      index: { live: 3, slots: 3, tombstones: 0, dim: 2 },
      embedding: { provider: 'hash', model: 'local-hash-v1', dim: 2 },
      chat: { provider: 'extractive', model: 'local-extractive-v1' },
      query_ef: 50,
      sample_loaded: true,
      benchmark_available: false,
    })
    apiMocks.health.mockReset().mockResolvedValue({
      status: 'ok',
      database: 'ok',
      store: { documents: 1, chunks: 3, embeddings: 3 },
      embedding: { provider: 'hash', model: 'local-hash-v1', available: true },
      chat: { provider: 'extractive', model: 'local-extractive-v1', available: true },
    })
    apiMocks.documents.mockReset().mockResolvedValue([sampleDocument])
    sessionStorage.clear()
  })

  it('offers starter questions only while the sample corpus is present', async () => {
    const { unmount } = renderAsk()
    expect(await screen.findByText('How does HNSW search work?')).toBeInTheDocument()
    unmount()

    apiMocks.documents.mockResolvedValue([{ ...sampleDocument, source: 'my-notes.md' }])
    renderAsk()
    expect(await screen.findByText('What topics do these documents cover?')).toBeInTheDocument()
    expect(screen.queryByText('How does HNSW search work?')).not.toBeInTheDocument()
  })

  it('clears the previous answer when a new question fails', async () => {
    const user = userEvent.setup()
    sessionStorage.setItem('hnswhy-last-answer', JSON.stringify(storedAnswer))
    apiMocks.ask.mockRejectedValue(new Error('The embedding provider is offline.'))
    renderAsk()

    expect(await screen.findByRole('heading', { name: 'Answer with citations' })).toBeInTheDocument()

    const input = screen.getByRole('textbox', { name: 'Your question' })
    await user.clear(input)
    await user.type(input, 'What does efSearch control?')
    await user.click(screen.getByRole('button', { name: 'Ask question' }))

    await waitFor(() =>
      expect(screen.queryByRole('heading', { name: 'Answer with citations' })).not.toBeInTheDocument(),
    )
    // The inline error and the toast both announce the failure.
    expect(screen.getAllByText('The embedding provider is offline.').length).toBeGreaterThan(0)
  })
})
