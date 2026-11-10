import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { describe, expect, it, vi } from 'vitest'
import { DocumentRow } from './DocumentRow'

const document = {
  id: 7,
  title: 'HNSW handbook',
  source: 'handbook.md',
  chunk_count: 12,
  char_count: 4821,
  created_at: '2026-11-10T14:04:58Z',
}

describe('DocumentRow', () => {
  it('shows document metadata and exposes inspect and delete actions', async () => {
    const user = userEvent.setup()
    const onInspect = vi.fn()
    const onDelete = vi.fn()

    render(<DocumentRow document={document} onInspect={onInspect} onDelete={onDelete} />)

    expect(screen.getByRole('heading', { name: 'HNSW handbook' })).toBeInTheDocument()
    expect(screen.getByText('handbook.md')).toBeInTheDocument()
    expect(screen.getByText('12')).toBeInTheDocument()
    expect(screen.getByText('4,821')).toBeInTheDocument()
    expect(screen.getByText('Markdown')).toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: 'Inspect HNSW handbook' }))
    expect(onInspect).toHaveBeenCalledOnce()

    await user.click(screen.getByRole('button', { name: 'Delete HNSW handbook' }))
    expect(onDelete).toHaveBeenCalledOnce()
  })
})
