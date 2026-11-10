import { useMemo, useState } from 'react'
import { api } from '../../api/client'
import type { DocumentSummary } from '../../api/types'
import { useData } from '../../app/DataProvider'
import { useToast } from '../../app/ToastProvider'
import { formatNumber, pluralize } from '../../lib/format'
import { Badge } from '../../ui/Badge'
import { Button } from '../../ui/Button'
import { ConfirmDialog } from '../../ui/Modal'
import { EmptyState, ErrorNotice, Skeleton } from '../../ui/States'
import { Icon } from '../../ui/Icon'
import { MetricCard, PageHeader, SectionHeader } from '../../ui/PageHeader'
import { DocumentDialog } from './DocumentDialog'
import { DocumentRow } from './DocumentRow'
import { UploadDialog } from './UploadDialog'

export function DocumentsPage() {
  const { documents, status, loading, error, refreshAll } = useData()
  const { pushToast } = useToast()
  const [uploadOpen, setUploadOpen] = useState(false)
  const [detailId, setDetailId] = useState<number | null>(null)
  const [deleteTarget, setDeleteTarget] = useState<DocumentSummary | null>(null)
  const [deleting, setDeleting] = useState(false)
  const [filter, setFilter] = useState('')

  const filtered = useMemo(() => {
    const needle = filter.trim().toLowerCase()
    if (!needle) {
      return documents
    }
    return documents.filter(
      (document) =>
        document.title.toLowerCase().includes(needle) ||
        document.source.toLowerCase().includes(needle),
    )
  }, [documents, filter])

  const removeDocument = async () => {
    if (!deleteTarget) {
      return
    }
    setDeleting(true)
    try {
      await api.deleteDocument(deleteTarget.id)
      const refreshed = await refreshAll()
      if (detailId === deleteTarget.id) {
        setDetailId(null)
      }
      pushToast(
        refreshed
          ? {
              tone: 'success',
              title: 'Document deleted',
              message: `“${deleteTarget.title}” and its vectors were removed.`,
            }
          : {
              tone: 'info',
              title: 'Document deleted',
              message: 'The list could not be refreshed. Retry to sync the view.',
            },
      )
      setDeleteTarget(null)
    } catch (requestError) {
      pushToast({
        tone: 'error',
        title: 'Could not delete the document',
        message: requestError instanceof Error ? requestError.message : undefined,
      })
    } finally {
      setDeleting(false)
    }
  }

  return (
    <div className="page">
      <PageHeader
        eyebrow="Knowledge base"
        title="Documents"
        description="Upload PDFs, Markdown, or plain text. HNSWhy extracts, chunks, embeds, and indexes each document without rebuilding the graph."
        actions={
          <>
            <Button
              variant="secondary"
              onClick={() => setDetailId(documents[0]?.id ?? null)}
              disabled={!documents.length}
              icon={<Icon name="panel" size={16} />}
            >
              Open latest
            </Button>
            <Button variant="primary" onClick={() => setUploadOpen(true)} icon={<Icon name="plus" size={17} />}>
              Add documents
            </Button>
          </>
        }
      />

      {error ? <ErrorNotice message={error} retry={() => void refreshAll()} /> : null}

      <div className="metric-grid">
        <MetricCard
          icon="documents"
          label="Documents"
          value={formatNumber(documents.length)}
          detail="in this library"
        />
        <MetricCard
          icon="layers"
          label="Chunks"
          // Unknown values render as an em dash rather than a misleading zero.
          value={status ? formatNumber(status.store.chunks) : '—'}
          detail="retrieval units"
        />
        <MetricCard
          icon="database"
          label="Live vectors"
          value={status ? formatNumber(status.index.live) : '—'}
          detail={
            !status
              ? 'waiting for the server'
              : status.index.tombstones
                ? `${formatNumber(status.index.tombstones)} tombstones`
                : 'indexed in memory'
          }
          tone="accent"
        />
        <MetricCard
          icon="grid"
          label="Dimensions"
          value={status ? String(status.embedding.dim) : '—'}
          detail={status ? `${status.embedding.provider} · ${status.embedding.model}` : 'provider pending'}
        />
      </div>

      {documents.some((document) => document.source === 'sample') ? (
        <div className="sample-notice">
          <span className="sample-notice__icon">
            <Icon name="sparkles" size={18} />
          </span>
          <div>
            <strong>A sample knowledge base is loaded</strong>
            <p>
              A starter corpus about HNSW, RAG, and HNSWhy ships with a fresh database so the
              workspace is useful immediately. Your uploads live alongside it.
            </p>
          </div>
          <Badge tone="accent">starter corpus</Badge>
        </div>
      ) : null}

      <section className="library" aria-labelledby="library-heading">
        <SectionHeader
          id="library-heading"
          title="Library"
          description={
            documents.length
              ? `${pluralize(documents.length, 'document')} · ${pluralize(status?.store.chunks ?? 0, 'chunk')}`
              : 'Nothing here yet'
          }
          action={
            <label className="search">
              <Icon name="search" size={15} />
              <span className="sr-only">Filter documents</span>
              <input
                type="search"
                placeholder="Filter by title or source…"
                value={filter}
                onChange={(event) => setFilter(event.target.value)}
              />
            </label>
          }
        />

        {loading && documents.length === 0 ? (
          <div className="document-list" aria-busy="true">
            {[0, 1, 2].map((item) => (
              <div className="document-row document-row--skeleton" key={item}>
                <Skeleton className="skeleton--avatar" />
                <div className="document-row__body">
                  <Skeleton className="skeleton--title" />
                  <Skeleton className="skeleton--line" />
                </div>
                <Skeleton className="skeleton--pill" />
              </div>
            ))}
          </div>
        ) : documents.length === 0 ? (
          // When the request failed, the ErrorNotice above already explains the
          // empty list; an "upload your first document" prompt would be wrong.
          error ? null : (
            <EmptyState
              icon="upload"
              title="Build your first knowledge base"
              description="Upload a PDF, a Markdown file, or paste text. It will be chunked, embedded, and ready to question in seconds."
              action={
                <Button variant="primary" onClick={() => setUploadOpen(true)} icon={<Icon name="plus" size={16} />}>
                  Add your first document
                </Button>
              }
            />
          )
        ) : filtered.length === 0 ? (
          <EmptyState
            icon="search"
            compact
            title="No matching documents"
            description={`Nothing matches “${filter}”. Try a shorter title or source name.`}
            action={<Button variant="ghost" onClick={() => setFilter('')}>Clear filter</Button>}
          />
        ) : (
          <div className="document-list">
            {filtered.map((document) => (
              <DocumentRow
                key={document.id}
                document={document}
                onInspect={() => setDetailId(document.id)}
                onDelete={() => setDeleteTarget(document)}
              />
            ))}
          </div>
        )}
      </section>

      <UploadDialog
        open={uploadOpen}
        onClose={() => setUploadOpen(false)}
        onUploaded={() => {
          void refreshAll().then((ok) => {
            if (!ok) {
              pushToast({
                tone: 'info',
                title: 'Document indexed',
                message: 'The library list could not be refreshed. Retry to sync the view.',
              })
            }
          })
        }}
      />

      <DocumentDialog
        documentId={detailId}
        onClose={() => setDetailId(null)}
        onDelete={(document) => {
          setDetailId(null)
          setDeleteTarget(document)
        }}
      />

      <ConfirmDialog
        open={deleteTarget !== null}
        onClose={() => setDeleteTarget(null)}
        onConfirm={() => void removeDocument()}
        busy={deleting}
        title="Delete this document?"
        confirmLabel="Delete document"
        description={
          deleteTarget ? (
            <>
              <strong>{deleteTarget.title}</strong> has {pluralize(deleteTarget.chunk_count, 'chunk')}.
              Deleting it removes the extracted text, embeddings, and searchable vectors. This cannot
              be undone.
            </>
          ) : null
        }
      />
    </div>
  )
}
