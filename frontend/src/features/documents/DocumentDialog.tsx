import { useEffect, useRef, useState, type KeyboardEvent } from 'react'
import { api } from '../../api/client'
import type { DocumentDetail } from '../../api/types'
import { fileKind, formatDate, formatNumber, pluralize, truncate } from '../../lib/format'
import { Badge } from '../../ui/Badge'
import { Button } from '../../ui/Button'
import { Icon } from '../../ui/Icon'
import { Modal } from '../../ui/Modal'
import { ErrorNotice, Skeleton } from '../../ui/States'

type Tab = 'text' | 'chunks'

export function DocumentDialog({
  documentId,
  onClose,
  onDelete,
}: {
  documentId: number | null
  onClose: () => void
  onDelete: (document: DocumentDetail['document']) => void
}) {
  const [detail, setDetail] = useState<DocumentDetail | null>(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [tab, setTab] = useState<Tab>('text')
  const [reloadKey, setReloadKey] = useState(0)
  const textTabRef = useRef<HTMLButtonElement>(null)
  const chunksTabRef = useRef<HTMLButtonElement>(null)

  const selectTab = (next: Tab) => {
    setTab(next)
    window.requestAnimationFrame(() => {
      ;(next === 'text' ? textTabRef : chunksTabRef).current?.focus()
    })
  }

  const onTabKeyDown = (event: KeyboardEvent<HTMLButtonElement>) => {
    if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) {
      return
    }
    event.preventDefault()
    selectTab(event.key === 'ArrowLeft' || event.key === 'Home' ? 'text' : 'chunks')
  }

  useEffect(() => {
    if (documentId === null) {
      setDetail(null)
      return
    }
    let active = true
    setLoading(true)
    setError(null)
    setTab('text')
    api
      .document(documentId)
      .then((next) => {
        if (active) {
          setDetail(next)
        }
      })
      .catch((requestError: unknown) => {
        if (active) {
          setError(requestError instanceof Error ? requestError.message : 'Could not load the document.')
        }
      })
      .finally(() => {
        if (active) {
          setLoading(false)
        }
      })
    return () => {
      active = false
    }
  }, [documentId, reloadKey])

  const footer = detail ? (
    <>
      <Button
        variant="danger"
        onClick={() => onDelete(detail.document)}
        icon={<Icon name="trash" size={15} />}
      >
        Delete
      </Button>
      <span className="dialog__spacer" />
      <Button variant="ghost" onClick={onClose}>
        Close
      </Button>
    </>
  ) : (
    <Button variant="ghost" onClick={onClose}>
      Close
    </Button>
  )

  return (
    <Modal
      open={documentId !== null}
      onClose={onClose}
      title={detail?.document.title ?? 'Document'}
      description={detail ? detail.document.source : 'Extracted text and indexed chunks'}
      size="xl"
      footer={footer}
    >
      {loading ? (
        <div className="detail-loading" role="status" aria-busy="true" aria-label="Loading document">
          <Skeleton className="skeleton--title" />
          <Skeleton className="skeleton--line" />
          <Skeleton className="skeleton--block" />
        </div>
      ) : error ? (
        <ErrorNotice message={error} retry={() => setReloadKey((key) => key + 1)} />
      ) : detail ? (
        <div className="detail">
          <div className="detail__meta">
            <Badge tone="neutral">{fileKind(detail.document.source)}</Badge>
            <span>{pluralize(detail.document.chunk_count, 'chunk')}</span>
            <span>{formatNumber(detail.document.char_count)} characters</span>
            <span>Added {formatDate(detail.document.created_at)}</span>
          </div>

          <div className="segmented segmented--inline" role="tablist" aria-label="Document view">
            <button
              ref={textTabRef}
              type="button"
              role="tab"
              id="document-tab-text"
              aria-controls="document-panel-text"
              aria-selected={tab === 'text'}
              tabIndex={tab === 'text' ? 0 : -1}
              className={tab === 'text' ? 'segmented__item is-active' : 'segmented__item'}
              onClick={() => setTab('text')}
              onKeyDown={onTabKeyDown}
            >
              Extracted text
            </button>
            <button
              ref={chunksTabRef}
              type="button"
              role="tab"
              id="document-tab-chunks"
              aria-controls="document-panel-chunks"
              aria-selected={tab === 'chunks'}
              tabIndex={tab === 'chunks' ? 0 : -1}
              className={tab === 'chunks' ? 'segmented__item is-active' : 'segmented__item'}
              onClick={() => setTab('chunks')}
              onKeyDown={onTabKeyDown}
            >
              Chunks <span className="segmented__count">{detail.chunks.length}</span>
            </button>
          </div>

          {tab === 'text' ? (
            <div
              className="detail__text"
              id="document-panel-text"
              role="tabpanel"
              aria-labelledby="document-tab-text"
              tabIndex={0}
            >
              {detail.text}
            </div>
          ) : (
            <div
              className="chunk-list"
              id="document-panel-chunks"
              role="tabpanel"
              aria-labelledby="document-tab-chunks"
              tabIndex={0}
            >
              {detail.chunks.map((chunk) => (
                <article key={chunk.id} className="chunk">
                  <header className="chunk__head">
                    <span className="chunk__index">{String(chunk.ordinal + 1).padStart(2, '0')}</span>
                    <span>chunk #{chunk.id}</span>
                  </header>
                  <p>{truncate(chunk.text, 420)}</p>
                </article>
              ))}
            </div>
          )}
        </div>
      ) : null}
    </Modal>
  )
}
