import { useEffect, useRef, useState, type DragEvent } from 'react'
import { api } from '../../api/client'
import type { DocumentSummary } from '../../api/types'
import { useToast } from '../../app/ToastProvider'
import { formatNumber, pluralize } from '../../lib/format'
import { Button } from '../../ui/Button'
import { Icon } from '../../ui/Icon'
import { Modal } from '../../ui/Modal'
import { InlineError } from '../../ui/States'

const MAX_FILE_BYTES = 20 * 1024 * 1024
const MAX_TEXT_BYTES = 4 * 1024 * 1024
const ACCEPTED = ['pdf', 'md', 'markdown', 'txt']
const TEXT_EXTENSIONS = ['md', 'markdown', 'txt']

type Mode = 'file' | 'text'

function validateFile(file: File): string | null {
  const extension = file.name.split('.').pop()?.toLowerCase() ?? ''
  if (!ACCEPTED.includes(extension)) {
    return 'Choose a PDF, Markdown, or plain-text file.'
  }
  if (file.size === 0) {
    return 'That file is empty.'
  }
  if (file.size > MAX_FILE_BYTES) {
    return 'That file is larger than the 20 MiB upload limit.'
  }
  // The server caps extracted text at 4 MiB even though a PDF may be 20 MiB.
  // Reject oversized text before uploading so the user gets the real limit.
  if (TEXT_EXTENSIONS.includes(extension) && file.size > MAX_TEXT_BYTES) {
    return 'Text and Markdown files are limited to 4 MiB. PDFs can be up to 20 MiB.'
  }
  return null
}

function utf8Bytes(value: string): number {
  return new TextEncoder().encode(value).length
}

function formatFileSize(bytes: number): string {
  if (bytes < 1024) {
    return `${bytes} B`
  }
  return `${(bytes / 1024).toFixed(0)} KB`
}

export function UploadDialog({
  open,
  onClose,
  onUploaded,
}: {
  open: boolean
  onClose: () => void
  onUploaded: (document: DocumentSummary) => void
}) {
  const { pushToast } = useToast()
  const [mode, setMode] = useState<Mode>('file')
  const [file, setFile] = useState<File | null>(null)
  const [title, setTitle] = useState('')
  const [source, setSource] = useState('paste')
  const [text, setText] = useState('')
  const [dragging, setDragging] = useState(false)
  const [busy, setBusy] = useState(false)
  const [progress, setProgress] = useState(0)
  const [error, setError] = useState<string | null>(null)
  const fileInputRef = useRef<HTMLInputElement>(null)
  const titleRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    if (!open) {
      return
    }
    setMode('file')
    setFile(null)
    setTitle('')
    setSource('paste')
    setText('')
    setDragging(false)
    setBusy(false)
    setProgress(0)
    setError(null)
  }, [open])

  const chooseFile = (candidate: File | undefined) => {
    if (!candidate) {
      return
    }
    const problem = validateFile(candidate)
    if (problem) {
      setError(problem)
      setFile(null)
      return
    }
    setError(null)
    setFile(candidate)
  }

  const onDrop = (event: DragEvent<HTMLDivElement>) => {
    event.preventDefault()
    setDragging(false)
    chooseFile(event.dataTransfer.files[0])
  }

  const submit = async () => {
    setError(null)
    if (mode === 'file' && !file) {
      setError('Choose a file to upload.')
      return
    }
    if (mode === 'text') {
      if (!text.trim()) {
        setError('Paste some text before adding the document.')
        return
      }
      if (utf8Bytes(text) > MAX_TEXT_BYTES) {
        setError('The pasted text is larger than the 4 MiB document limit.')
        return
      }
    }

    setBusy(true)
    setProgress(0)
    try {
      const document =
        mode === 'file' && file
          ? await api.upload(file, title, setProgress)
          : (await api.addText(text, title, source.trim() || 'paste')).document

      pushToast({
        tone: 'success',
        title: 'Document added',
        message: `${pluralize(document.chunk_count, 'chunk')} ${
          document.chunk_count === 1 ? 'is' : 'are'
        } now searchable.`,
      })
      onUploaded(document)
      onClose()
    } catch (requestError) {
      setError(requestError instanceof Error ? requestError.message : 'The upload failed.')
    } finally {
      setBusy(false)
    }
  }

  return (
    <Modal
      open={open}
      onClose={onClose}
      title="Add to knowledge base"
      description="Text is extracted, chunked, embedded, and indexed incrementally."
      size="lg"
      busy={busy}
      initialFocusRef={titleRef}
      footer={
        <>
          <Button variant="ghost" onClick={onClose} disabled={busy}>
            Cancel
          </Button>
          <Button
            variant="primary"
            onClick={() => void submit()}
            loading={busy}
            icon={<Icon name="upload" size={16} />}
          >
            {mode === 'file' ? 'Upload and index' : 'Add and index'}
          </Button>
        </>
      }
    >
      <div className="segmented" role="group" aria-label="Document source">
        <button
          type="button"
          className={mode === 'file' ? 'segmented__item is-active' : 'segmented__item'}
          aria-pressed={mode === 'file'}
          onClick={() => setMode('file')}
          disabled={busy}
        >
          <Icon name="upload" size={15} /> Upload file
        </button>
        <button
          type="button"
          className={mode === 'text' ? 'segmented__item is-active' : 'segmented__item'}
          aria-pressed={mode === 'text'}
          onClick={() => setMode('text')}
          disabled={busy}
        >
          <Icon name="file" size={15} /> Paste text
        </button>
      </div>

      <div className="form-stack">
        <label className="field">
          <span className="field__label">
            Title <span className="field__optional">optional</span>
          </span>
          <input
            ref={titleRef}
            type="text"
            value={title}
            onChange={(event) => setTitle(event.target.value)}
            placeholder={file ? file.name.replace(/\.[^.]+$/, '') : 'A short, recognizable title'}
            maxLength={200}
            disabled={busy}
          />
          <span className="field__hint">
            {mode === 'file'
              ? 'Falls back to the file name when left empty.'
              : 'Used as the document title in the library.'}
          </span>
        </label>

        {mode === 'file' ? (
          <div
            className={`dropzone ${dragging ? 'is-dragging' : ''} ${file ? 'is-filled' : ''}`}
            role="button"
            tabIndex={0}
            aria-label={
              file ? `Selected file ${file.name}. Choose a different file` : 'Choose a document to upload'
            }
            aria-invalid={error ? true : undefined}
            aria-describedby={error ? 'upload-error' : undefined}
            onDragOver={(event) => {
              event.preventDefault()
              setDragging(true)
            }}
            onDragLeave={() => setDragging(false)}
            onDrop={onDrop}
            onClick={() => fileInputRef.current?.click()}
            onKeyDown={(event) => {
              if (event.key === 'Enter' || event.key === ' ') {
                event.preventDefault()
                fileInputRef.current?.click()
              }
            }}
          >
            <input
              ref={fileInputRef}
              type="file"
              accept=".pdf,.md,.markdown,.txt,application/pdf,text/plain,text/markdown"
              className="sr-only"
              tabIndex={-1}
              aria-hidden="true"
              disabled={busy}
              onChange={(event) => chooseFile(event.target.files?.[0])}
              onClick={(event) => event.stopPropagation()}
            />
            <span className={`dropzone__icon ${file ? 'dropzone__icon--ready' : ''}`}>
              <Icon name={file ? 'file' : 'upload'} size={22} />
            </span>
            {file ? (
              <>
                <strong>{file.name}</strong>
                <span className="dropzone__meta">
                  {formatFileSize(file.size)} · click to choose a different file
                </span>
              </>
            ) : (
              <>
                <strong>Drop a file here, or browse</strong>
                <span className="dropzone__meta">
                  PDF up to 20 MiB · Markdown or TXT up to 4 MiB
                </span>
              </>
            )}
          </div>
        ) : (
          <div className="form-grid">
            <label className="field">
              <span className="field__label">Source label</span>
              <input
                type="text"
                value={source}
                onChange={(event) => setSource(event.target.value)}
                placeholder="paste"
                maxLength={300}
                disabled={busy}
              />
            </label>
            <label className="field field--full">
              <span className="field__label">Text</span>
              <textarea
                value={text}
                onChange={(event) => setText(event.target.value)}
                placeholder="Paste notes, documentation, or an article here…"
                rows={9}
                disabled={busy}
                aria-invalid={error ? true : undefined}
                aria-describedby={error ? 'upload-error' : undefined}
              />
              <span className="field__hint">
                {formatNumber(text.length)} characters · 4 MiB maximum
              </span>
            </label>
          </div>
        )}

        {busy && mode === 'file' ? (
          <div className="upload-progress" role="status">
            <div className="upload-progress__row">
              <span>{progress < 100 ? 'Uploading…' : 'Chunking, embedding, and indexing…'}</span>
              <span>{progress}%</span>
            </div>
            <div className="progress-track">
              <span style={{ width: `${progress}%` }} />
            </div>
          </div>
        ) : null}

        {error ? <InlineError id="upload-error">{error}</InlineError> : null}
      </div>
    </Modal>
  )
}
