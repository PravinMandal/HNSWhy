import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import type { DocumentSummary } from '../../api/types'
import { ToastProvider } from '../../app/ToastProvider'
import { ToastViewport } from '../../ui/ToastViewport'
import { UploadDialog } from './UploadDialog'

const apiMocks = vi.hoisted(() => ({
  upload: vi.fn(),
  addText: vi.fn(),
}))

vi.mock('../../api/client', () => ({ api: apiMocks }))

const summary: DocumentSummary = {
  id: 7,
  title: 'Notes',
  source: 'notes.md',
  chunk_count: 2,
  char_count: 120,
  created_at: '2026-11-10T14:04:58Z',
}

function renderDialog(onUploaded = vi.fn()) {
  render(
    <ToastProvider>
      <UploadDialog open onClose={vi.fn()} onUploaded={onUploaded} />
      <ToastViewport />
    </ToastProvider>,
  )
  return { onUploaded }
}

function fileInput(): HTMLInputElement {
  const input = document.querySelector<HTMLInputElement>('input[type="file"]')
  if (!input) {
    throw new Error('file input not found')
  }
  return input
}

describe('UploadDialog', () => {
  beforeEach(() => {
    apiMocks.upload.mockReset()
    apiMocks.addText.mockReset()
  })

  it('rejects unsupported and oversized files before uploading', () => {
    renderDialog()

    fireEvent.change(fileInput(), {
      target: { files: [new File(['PK'], 'archive.zip', { type: 'application/zip' })] },
    })
    expect(screen.getByText('Choose a PDF, Markdown, or plain-text file.')).toBeInTheDocument()

    const oversized = new File(['text'], 'large.txt', { type: 'text/plain' })
    Object.defineProperty(oversized, 'size', { value: 5 * 1024 * 1024 })
    fireEvent.change(fileInput(), { target: { files: [oversized] } })
    expect(
      screen.getByText('Text and Markdown files are limited to 4 MiB. PDFs can be up to 20 MiB.'),
    ).toBeInTheDocument()
    expect(apiMocks.upload).not.toHaveBeenCalled()
  })

  it('clears a file error when the user switches to pasted text', async () => {
    const user = userEvent.setup()
    renderDialog()

    fireEvent.change(fileInput(), {
      target: { files: [new File(['PK'], 'archive.zip', { type: 'application/zip' })] },
    })
    expect(screen.getByText('Choose a PDF, Markdown, or plain-text file.')).toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: /Paste text/ }))

    expect(
      screen.queryByText('Choose a PDF, Markdown, or plain-text file.'),
    ).not.toBeInTheDocument()
    expect(screen.getByRole('textbox', { name: /^Text/ })).not.toHaveAttribute('aria-invalid')
  })

  it('uploads a valid file and reports the indexed document', async () => {
    const user = userEvent.setup()
    apiMocks.upload.mockResolvedValue(summary)
    const { onUploaded } = renderDialog()

    fireEvent.change(fileInput(), {
      target: { files: [new File(['hello'], 'notes.md', { type: 'text/markdown' })] },
    })
    await user.click(screen.getByRole('button', { name: 'Upload and index' }))

    await waitFor(() => expect(onUploaded).toHaveBeenCalledWith(summary))
    expect(apiMocks.upload).toHaveBeenCalledWith(
      expect.objectContaining({ name: 'notes.md' }),
      '',
      expect.any(Function),
    )
    expect(screen.getByText('Document added')).toBeInTheDocument()
  })

  it('adds pasted text with the edited source label', async () => {
    const user = userEvent.setup()
    apiMocks.addText.mockResolvedValue({ document: summary })
    const { onUploaded } = renderDialog()

    await user.click(screen.getByRole('button', { name: /Paste text/ }))
    await user.clear(screen.getByLabelText('Source label'))
    await user.type(screen.getByLabelText('Source label'), 'meeting-notes')
    await user.type(screen.getByRole('textbox', { name: /^Text/ }), 'The corpus is small.')
    await user.click(screen.getByRole('button', { name: 'Add and index' }))

    await waitFor(() => expect(onUploaded).toHaveBeenCalledWith(summary))
    expect(apiMocks.addText).toHaveBeenCalledWith('The corpus is small.', '', 'meeting-notes')
  })

  it('keeps the dialog open and shows the server error', async () => {
    const user = userEvent.setup()
    apiMocks.upload.mockRejectedValue(new Error('Embedding provider is offline.'))
    renderDialog()

    fireEvent.change(fileInput(), {
      target: { files: [new File(['hello'], 'notes.md', { type: 'text/markdown' })] },
    })
    await user.click(screen.getByRole('button', { name: 'Upload and index' }))

    expect(await screen.findByText('Embedding provider is offline.')).toBeInTheDocument()
    expect(screen.getByRole('dialog')).toBeInTheDocument()
  })
})
