import { useRef, useState } from 'react'
import type { AskResponse, AskSource } from '../../api/types'
import { useToast } from '../../app/ToastProvider'
import { formatMs, formatNumber, pluralize, truncate } from '../../lib/format'
import { Badge } from '../../ui/Badge'
import { Button } from '../../ui/Button'
import { Icon } from '../../ui/Icon'

const REFUSAL = 'I could not find an answer to that question in the uploaded documents.'

function AnswerText({
  text,
  sourceCount,
  onCitation,
}: {
  text: string
  sourceCount: number
  onCitation: (index: number) => void
}) {
  const parts = text.split(/(\[\d+\])/g)
  return (
    <div className="answer-text">
      {parts.map((part, index) => {
        const match = /^\[(\d+)\]$/.exec(part)
        if (!match) {
          return <span key={`text-${index}`}>{part}</span>
        }
        const citation = Number(match[1])
        if (citation < 1 || citation > sourceCount) {
          return <span key={`citation-${index}`}>{part}</span>
        }
        return (
          <button
            key={`citation-${index}`}
            type="button"
            className="citation"
            onClick={() => onCitation(citation)}
            aria-label={`Jump to source ${citation}`}
          >
            {match[1]}
          </button>
        )
      })}
    </div>
  )
}

function SourceCard({
  source,
  index,
  active,
  expanded,
  onToggle,
  cardRef,
}: {
  source: AskSource
  index: number
  active: boolean
  expanded: boolean
  onToggle: () => void
  cardRef: (element: HTMLElement | null) => void
}) {
  const classes = ['source', active ? 'is-active' : '', expanded ? 'is-expanded' : ''].filter(Boolean).join(' ')

  return (
    <article ref={cardRef} className={classes}>
      <button type="button" className="source__head" onClick={onToggle} aria-expanded={expanded}>
        <span className="source__number">{String(index).padStart(2, '0')}</span>
        <span className="source__title">
          <strong>{source.title}</strong>
          <span>
            chunk #{source.chunk_id} · position {source.ordinal + 1} · d² {source.distance.toFixed(2)}
          </span>
        </span>
        <Icon name="chevronDown" size={16} className={expanded ? 'is-open' : undefined} />
      </button>
      <p className="source__text">{expanded ? source.text : truncate(source.text, 230)}</p>
      {!expanded && source.text.length > 230 ? (
        <button type="button" className="source__more" onClick={onToggle}>
          Show full chunk
        </button>
      ) : null}
    </article>
  )
}

function TimingRow({ label, value, total }: { label: string; value: number; total: number }) {
  const width = total > 0 ? Math.max(4, (value / total) * 100) : 0
  return (
    <div className="timing">
      <div className="timing__head">
        <span>{label}</span>
        <strong>{formatMs(value)}</strong>
      </div>
      <div className="timing__track">
        <span style={{ width: `${width}%` }} />
      </div>
    </div>
  )
}

export function AnswerView({ answer, onOpenMap }: { answer: AskResponse; onOpenMap: () => void }) {
  const { pushToast } = useToast()
  const [activeSource, setActiveSource] = useState(0)
  const [expanded, setExpanded] = useState<Set<number>>(() => new Set([1]))
  const sourceRefs = useRef(new Map<number, HTMLElement>())
  const refused = answer.answer.trim() === REFUSAL
  const total = Math.max(answer.timings_ms.total, 0.0001)

  const focusSource = (index: number) => {
    setActiveSource(index)
    setExpanded((current) => new Set(current).add(index))
    window.requestAnimationFrame(() => {
      sourceRefs.current.get(index)?.scrollIntoView({ behavior: 'smooth', block: 'nearest' })
    })
  }

  const copyAnswer = async () => {
    try {
      await navigator.clipboard.writeText(answer.answer)
      pushToast({ tone: 'success', title: 'Answer copied to clipboard' })
    } catch {
      pushToast({ tone: 'error', title: 'Clipboard access is not available' })
    }
  }

  return (
    <>
      <section className={`answer ${refused ? 'answer--refusal' : ''}`} aria-labelledby="answer-heading">
        <header className="answer__head">
          <div className="answer__identity">
            <span className={`answer__mark ${refused ? 'answer__mark--refusal' : ''}`}>
              <Icon name={refused ? 'info' : 'sparkles'} size={19} />
            </span>
            <div>
              <h2 id="answer-heading">{refused ? 'No grounded answer' : 'Grounded answer'}</h2>
              <span>
                {refused
                  ? 'The retrieved context did not support an answer.'
                  : `${pluralize(answer.sources.length, 'source')} retrieved · ${formatMs(answer.timings_ms.total)} total`}
              </span>
            </div>
          </div>
          <div className="answer__actions">
            <Badge tone={refused ? 'warn' : 'accent'} dot>
              {refused ? 'refusal' : 'cited'}
            </Badge>
            <Button variant="ghost" size="sm" onClick={() => void copyAnswer()} icon={<Icon name="copy" size={15} />}>
              Copy
            </Button>
          </div>
        </header>

        {refused ? (
          <div className="refusal">
            <p>{answer.answer}</p>
            <span>Try a concept that appears in your documents, or upload a source that covers it.</span>
          </div>
        ) : (
          <AnswerText text={answer.answer} sourceCount={answer.sources.length} onCitation={focusSource} />
        )}

        <footer className="answer__foot">
          <span className="answer__meta">
            <Icon name="cpu" size={14} />
            {answer.models.chat_provider}/{answer.models.chat_model}
          </span>
          <span className="answer__meta">
            <Icon name="layers" size={14} />
            {answer.models.embedding_provider}/{answer.models.embedding_model}
          </span>
          <span className="answer__spacer" />
          <Button variant="secondary" size="sm" onClick={onOpenMap} icon={<Icon name="map" size={15} />}>
            View in map
          </Button>
        </footer>
      </section>

      <div className="answer-grid">
        <section className="sources" aria-labelledby="sources-heading">
          <div className="sources__head">
            <div>
              <h2 id="sources-heading">Retrieved chunks</h2>
              <p>Numbered in the same order as the citations above.</p>
            </div>
            <Badge tone="neutral">{formatNumber(answer.sources.length)}</Badge>
          </div>

          {answer.sources.length ? (
            <div className="source-list">
              {answer.sources.map((source, index) => (
                <SourceCard
                  key={`${source.chunk_id}-${index}`}
                  source={source}
                  index={index + 1}
                  active={activeSource === index + 1}
                  expanded={expanded.has(index + 1)}
                  onToggle={() => {
                    setActiveSource(index + 1)
                    setExpanded((current) => {
                      const next = new Set(current)
                      if (next.has(index + 1)) {
                        next.delete(index + 1)
                      } else {
                        next.add(index + 1)
                      }
                      return next
                    })
                  }}
                  cardRef={(element) => {
                    if (element) {
                      sourceRefs.current.set(index + 1, element)
                    } else {
                      sourceRefs.current.delete(index + 1)
                    }
                  }}
                />
              ))}
            </div>
          ) : (
            <p className="muted">No chunks were returned for this question.</p>
          )}
        </section>

        <aside className="answer-side" aria-label="Retrieval details">
          <div className="side-card">
            <div className="side-card__head">
              <Icon name="clock" size={16} />
              <h3>Pipeline timing</h3>
            </div>
            <div className="timing-list">
              <TimingRow label="Embed question" value={answer.timings_ms.embedding} total={total} />
              <TimingRow label="HNSW search" value={answer.timings_ms.search} total={total} />
              <TimingRow label="Generate answer" value={answer.timings_ms.generate} total={total} />
            </div>
            <div className="timing-total">
              <span>End to end</span>
              <strong>{formatMs(answer.timings_ms.total)}</strong>
            </div>
          </div>

          <div className="side-card">
            <div className="side-card__head">
              <Icon name="layers" size={16} />
              <h3>Models</h3>
            </div>
            <dl className="model-list">
              <div>
                <dt>Embedding</dt>
                <dd>{answer.models.embedding_model}</dd>
              </div>
              <div>
                <dt>Provider</dt>
                <dd>{answer.models.embedding_provider}</dd>
              </div>
              <div>
                <dt>Chat</dt>
                <dd>{answer.models.chat_model}</dd>
              </div>
              <div>
                <dt>Provider</dt>
                <dd>{answer.models.chat_provider}</dd>
              </div>
            </dl>
          </div>

          <div className="side-card side-card--accent">
            <div className="side-card__head">
              <Icon name="info" size={16} />
              <h3>About this answer</h3>
            </div>
            <p>
              The answer is generated only from the retrieved chunks. Distances are squared L2
              values in the active embedding space, so smaller is closer.
            </p>
          </div>
        </aside>
      </div>
    </>
  )
}
