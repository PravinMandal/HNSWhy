import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { describe, expect, it, vi } from 'vitest'
import type { AskResponse } from '../../api/types'
import { ToastProvider } from '../../app/ToastProvider'
import { AnswerView } from './AnswerView'

const baseAnswer: AskResponse = {
  answer: '**HNSW** builds a layered graph [1] and searches it from the top [2].',
  sources: [
    {
      chunk_id: 1,
      document_id: 10,
      title: 'HNSW handbook',
      ordinal: 0,
      text: 'HNSW builds a layered proximity graph and descends through upper layers before layer zero.',
      distance: 0.12,
    },
    {
      chunk_id: 2,
      document_id: 10,
      title: 'Search notes',
      ordinal: 1,
      text: 'Search starts at the entry point and explores a bounded candidate list.',
      distance: 0.34,
    },
  ],
  query_vector: [0.1, 0.2],
  timings_ms: { embedding: 1, search: 2, generate: 3, total: 6 },
  models: {
    embedding_provider: 'hash',
    embedding_model: 'local-hash-v1',
    chat_provider: 'extractive',
    chat_model: 'local-extractive-v1',
  },
}

function renderAnswer(answer: AskResponse) {
  return render(
    <ToastProvider>
      <AnswerView answer={answer} onOpenMap={vi.fn()} />
    </ToastProvider>,
  )
}

describe('AnswerView', () => {
  it('renders Markdown, citations, sources and pipeline details', async () => {
    const user = userEvent.setup()
    const { container } = renderAnswer(baseAnswer)

    expect(screen.getByRole('heading', { name: 'Answer with citations' })).toBeInTheDocument()
    expect(screen.getByText('HNSW', { selector: 'strong' })).toBeInTheDocument()
    expect(screen.getByText('cited')).toBeInTheDocument()
    expect(screen.getByText('HNSW handbook')).toBeInTheDocument()
    expect(screen.getByText('local-hash-v1')).toBeInTheDocument()
    expect(screen.getByText('6.00 ms')).toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: 'Jump to source 2' }))

    const source = container.querySelectorAll<HTMLElement>('.source')[1]
    expect(source).toHaveClass('is-active')
    await waitFor(() => expect(source).toHaveFocus())
  })

  it('warns when an answer has no citation markers', () => {
    renderAnswer({ ...baseAnswer, answer: 'A fluent answer with no citation.' })

    expect(screen.getByRole('heading', { name: 'Answer without citations' })).toBeInTheDocument()
    expect(screen.getByText('uncited')).toBeInTheDocument()
    expect(
      screen.getByText(/cannot be traced back to the retrieved chunks/i),
    ).toBeInTheDocument()
  })

  it('labels the offline refusal and does not show the uncited warning', () => {
    renderAnswer({
      ...baseAnswer,
      answer: 'I could not find an answer to that question in the uploaded documents.',
    })

    expect(screen.getByRole('heading', { name: 'No answer found' })).toBeInTheDocument()
    expect(screen.getByText('refusal')).toBeInTheDocument()
    expect(screen.queryByText(/cannot be traced back/i)).not.toBeInTheDocument()
  })

  it('does not turn index expressions inside code into citations', () => {
    const { container } = renderAnswer({
      ...baseAnswer,
      answer: 'Inline `values[1]` stays code, and so does:\n\n```\narray[1] = 2\n```\n\nBut source [1] is a real citation.',
    })

    expect(container.querySelectorAll('code')[0]).toHaveTextContent('values[1]')
    expect(container.querySelectorAll('pre code')[0]).toHaveTextContent('array[1] = 2')
    expect(screen.getAllByRole('button', { name: /Jump to source/ })).toHaveLength(1)
  })

  it('does not render raw HTML or unsafe links from a model answer', () => {
    const { container } = renderAnswer({
      ...baseAnswer,
      answer: '<script>alert(1)</script>\n\n[unsafe](javascript:alert(1))',
    })

    expect(container.querySelector('script')).not.toBeInTheDocument()
    expect(container.querySelector('a[href^="javascript"]')).not.toBeInTheDocument()
  })
})
