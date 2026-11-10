import { fileKind, formatDate, formatNumber } from '../../lib/format'
import type { DocumentSummary } from '../../api/types'
import { Badge } from '../../ui/Badge'
import { Button } from '../../ui/Button'
import { Icon } from '../../ui/Icon'

export function DocumentRow({
  document,
  onInspect,
  onDelete,
}: {
  document: DocumentSummary
  onInspect: () => void
  onDelete: () => void
}) {
  const kind = fileKind(document.source)

  return (
    <article className="document-row">
      <span className={`document-row__icon document-row__icon--${kind.toLowerCase()}`}>
        <Icon name="file" size={19} />
      </span>

      <div className="document-row__body">
        <div className="document-row__title">
          <h3>{document.title}</h3>
          <Badge tone="neutral">{kind}</Badge>
        </div>
        <span className="document-row__source" title={document.source}>
          {document.source || 'pasted text'}
        </span>
      </div>

      <div className="document-row__metric">
        <strong>{formatNumber(document.chunk_count)}</strong>
        <span>chunks</span>
      </div>
      <div className="document-row__metric document-row__metric--wide">
        <strong>{formatNumber(document.char_count)}</strong>
        <span>characters</span>
      </div>
      <div className="document-row__date">{formatDate(document.created_at)}</div>

      <div className="document-row__actions">
        <Button
          variant="quiet"
          size="sm"
          onClick={onInspect}
          aria-label={`Inspect ${document.title}`}
          trailingIcon={<Icon name="arrowRight" size={14} />}
        >
          Inspect
        </Button>
        <Button
          variant="ghost"
          size="sm"
          onClick={onDelete}
          aria-label={`Delete ${document.title}`}
          icon={<Icon name="trash" size={15} />}
        />
      </div>
    </article>
  )
}
