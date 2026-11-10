import { useId, type FormEvent, type KeyboardEvent } from 'react'
import { pluralize } from '../../lib/format'
import { Badge } from '../../ui/Badge'
import { Button } from '../../ui/Button'
import { Icon } from '../../ui/Icon'
import { InlineError } from '../../ui/States'

export function AskComposer({
  question,
  onQuestionChange,
  k,
  onKChange,
  busy,
  error,
  documentCount,
  onSubmit,
}: {
  question: string
  onQuestionChange: (value: string) => void
  k: number
  onKChange: (value: number) => void
  busy: boolean
  error: string | null
  documentCount: number
  onSubmit: () => void
}) {
  const inputId = useId()
  const errorId = `${inputId}-error`

  const submit = (event: FormEvent) => {
    event.preventDefault()
    onSubmit()
  }

  const onKeyDown = (event: KeyboardEvent<HTMLTextAreaElement>) => {
    if ((event.metaKey || event.ctrlKey) && event.key === 'Enter') {
      event.preventDefault()
      onSubmit()
    }
  }

  return (
    <form className="composer" onSubmit={submit} aria-labelledby={`${inputId}-label`}>
      <div className="composer__head">
        <label id={`${inputId}-label`} htmlFor={inputId} className="composer__label">
          Your question
        </label>
        <div className="composer__badges">
          <Badge tone="neutral">{pluralize(documentCount, 'document')} available</Badge>
        </div>
      </div>

      <textarea
        id={inputId}
        className="composer__input"
        value={question}
        onChange={(event) => onQuestionChange(event.target.value)}
        onKeyDown={onKeyDown}
        placeholder="What would you like to know?"
        rows={3}
        maxLength={2000}
        disabled={busy}
        autoFocus
        aria-invalid={error ? true : undefined}
        aria-describedby={error ? errorId : undefined}
      />

      <div className="composer__foot">
        <div className="composer__controls">
          <label className="select">
            <span>Sources</span>
            <select value={k} onChange={(event) => onKChange(Number(event.target.value))} disabled={busy}>
              <option value={3}>3 chunks</option>
              <option value={5}>5 chunks</option>
              <option value={8}>8 chunks</option>
              <option value={10}>10 chunks</option>
            </select>
          </label>
          <span className="composer__hint">Ctrl / ⌘ + Enter to ask</span>
          <span className="composer__count">{question.length}/2000</span>
        </div>
        <Button
          type="submit"
          variant="primary"
          loading={busy}
          disabled={documentCount === 0}
          icon={<Icon name="sparkles" size={16} />}
        >
          Ask question
        </Button>
      </div>

      {error ? <InlineError id={errorId}>{error}</InlineError> : null}
    </form>
  )
}
