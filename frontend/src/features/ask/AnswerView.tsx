import { useMemo, useRef, useState } from 'react'
import Markdown, { defaultUrlTransform, type Components } from 'react-markdown'
import rehypeSanitize, { defaultSchema } from 'rehype-sanitize'
import type { AskResponse, AskSource } from '../../api/types'
import { useToast } from '../../app/ToastProvider'
import { formatMs, formatNumber, pluralize, truncate } from '../../lib/format'
import { scrollBehavior } from '../../lib/motion'
import { Badge } from '../../ui/Badge'
import { Button } from '../../ui/Button'
import { Icon } from '../../ui/Icon'

const REFUSAL = 'I could not find an answer to that question in the uploaded documents.'

// Answers may come from a language model, so render Markdown with a strict
// allow-list. Raw HTML is never enabled, and images are excluded because they
// can leak the reader's IP to a third-party host.
const MARKDOWN_ELEMENTS = [
  'p', 'br', 'strong', 'em', 'del', 'code', 'pre', 'blockquote',
  'ul', 'ol', 'li', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6',
  'a', 'hr', 'table', 'thead', 'tbody', 'tr', 'th', 'td',
]

const citationSchema = {
  ...defaultSchema,
  protocols: {
    ...defaultSchema.protocols,
    href: [...(defaultSchema.protocols?.href ?? []), 'citation'],
  },
}

// Split Markdown into prose and code segments: backtick fences, tilde fences,
// and inline spans. String.split with one capture group alternates prose and
// code, so even indices are prose and odd indices are code. Both citation
// detection and citation rewriting use this so they agree on what is a real
// marker rather than `array[1]` in a code sample.
const CODE_SEGMENTS = /(```[\s\S]*?```|~~~[\s\S]*?~~~|`[^`\n]*`)/g

function proseSegments(markdown: string): string[] {
  return markdown.split(CODE_SEGMENTS).filter((_, index) => index % 2 === 0)
}

function citationNumbers(text: string, sourceCount: number): number[] {
  return proseSegments(text).flatMap((segment) =>
    Array.from(segment.matchAll(/\[(\d+)\](?!\()/g))
      .map((match) => Number(match[1]))
      .filter((index) => index >= 1 && index <= sourceCount),
  )
}

/**
 * Rewrites bracketed citation markers into Markdown links. Code spans and
 * fenced blocks are left untouched so `array[1]` in a snippet is not turned
 * into a citation, and markers that already belong to a Markdown link are
 * skipped so an existing URL is never rewritten.
 */
export function linkCitations(markdown: string, sourceCount: number): string {
  return markdown
    .split(CODE_SEGMENTS)
    .map((segment, index) => {
      if (index % 2 === 1) {
        return segment
      }
      return segment.replace(/\[(\d+)\](?!\()/g, (match, digits: string) => {
        const citation = Number(digits)
        return citation >= 1 && citation <= sourceCount
          ? `[${digits}](citation:${citation})`
          : match
      })
    })
    .join('')
}

function AnswerText({
  text,
  sourceCount,
  onCitation,
}: {
  text: string
  sourceCount: number
  onCitation: (index: number) => void
}) {
  const markdown = linkCitations(text, sourceCount)

  const components: Components = {
    a({ href, children }) {
      if (href?.startsWith('citation:')) {
        const citation = Number(href.slice('citation:'.length))
        return (
          <button
            type="button"
            className="citation"
            onClick={() => onCitation(citation)}
            aria-label={`Jump to source ${citation}`}
          >
            {children}
          </button>
        )
      }
      return (
        <a href={href} target="_blank" rel="noreferrer">
          {children}
        </a>
      )
    },
  }

  return (
    <div className="answer-text">
      <Markdown
        allowedElements={MARKDOWN_ELEMENTS}
        components={components}
        rehypePlugins={[[rehypeSanitize, citationSchema]]}
        urlTransform={(url) => (url.startsWith('citation:') ? url : defaultUrlTransform(url))}
      >
        {markdown}
      </Markdown>
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
    <article ref={cardRef} className={classes} tabIndex={-1}>
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
      {source.text.length > 230 ? (
        // Keep this control mounted while the card expands. Unmounting it
        // would drop keyboard focus to <body> right after activation.
        <button
          type="button"
          className="source__more"
          onClick={onToggle}
          aria-expanded={expanded}
        >
          {expanded ? 'Show less' : 'Show full chunk'}
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
  const cited = useMemo(
    () => citationNumbers(answer.answer, answer.sources.length).length > 0,
    [answer.answer, answer.sources.length],
  )
  const total = Math.max(answer.timings_ms.total, 0.0001)

  const focusSource = (index: number) => {
    setActiveSource(index)
    setExpanded((current) => new Set(current).add(index))
    window.requestAnimationFrame(() => {
      const source = sourceRefs.current.get(index)
      source?.scrollIntoView({ behavior: scrollBehavior(), block: 'nearest' })
      // Scroll is visual only; move focus so keyboard and screen-reader users
      // land on the cited chunk too.
      source?.focus({ preventScroll: true })
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
              <h2 id="answer-heading" tabIndex={-1}>
                {refused ? 'No answer found' : cited ? 'Answer with citations' : 'Answer from general knowledge'}
              </h2>
              <span>
                {refused
                  ? 'The answer provider did not return an answer.'
                  : cited
                    ? `${pluralize(answer.sources.length, 'source')} retrieved · ${formatMs(answer.timings_ms.total)} total`
                    : answer.sources.length > 0
                      ? `${pluralize(answer.sources.length, 'source')} retrieved · general knowledge · ${formatMs(answer.timings_ms.total)} total`
                      : `General knowledge · ${formatMs(answer.timings_ms.total)} total`}
              </span>
            </div>
          </div>
          <div className="answer__actions">
            <Badge tone={refused || !cited ? 'warn' : 'accent'} dot>
              {refused ? 'refusal' : cited ? 'cited' : 'general'}
            </Badge>
            <Button variant="ghost" size="sm" onClick={() => void copyAnswer()} icon={<Icon name="copy" size={15} />}>
              Copy
            </Button>
          </div>
        </header>

        {refused ? (
          <div className="refusal">
            <p>{answer.answer}</p>
            <span>
              No retrieved chunk answered this question. Upload a source that covers it, or use
              a chat model that can answer from general knowledge.
            </span>
          </div>
        ) : (
          <>
            {!cited ? (
              <p className="answer-warning" role="note">
                {answer.sources.length > 0
                  ? 'This answer did not use the retrieved chunks. It comes from the model’s general knowledge; verify important claims before relying on it.'
                  : 'No document chunks matched this question. The answer comes from the model’s general knowledge; verify important claims before relying on it.'}
              </p>
            ) : null}
            <AnswerText text={answer.answer} sourceCount={answer.sources.length} onCitation={focusSource} />
          </>
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
              <p>
                {cited
                  ? 'Numbered in the same order as the citations above.'
                  : 'Retrieved for context; this answer did not use them.'}
              </p>
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
              Answers use the retrieved chunks when they help and cite them. When the chunks do
              not cover the question, the model answers from general knowledge; those answers
              are marked as general knowledge. Distances are squared L2 values in the active
              embedding space, so smaller is closer.
            </p>
          </div>
        </aside>
      </div>
    </>
  )
}
