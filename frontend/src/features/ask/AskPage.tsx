import { useRef, useState } from 'react'
import { api } from '../../api/client'
import { useData } from '../../app/DataProvider'
import { useToast } from '../../app/ToastProvider'
import { scrollBehavior } from '../../lib/motion'
import { Button } from '../../ui/Button'
import { EmptyState, ErrorNotice, Skeleton } from '../../ui/States'
import { Icon } from '../../ui/Icon'
import { PageHeader } from '../../ui/PageHeader'
import { AnswerView } from './AnswerView'
import { AskComposer } from './AskComposer'

// These prompts only make sense while the bundled starter corpus is present.
const SAMPLE_SUGGESTIONS = [
  'How does HNSW search work?',
  'How does HNSWhy keep SQLite and the index consistent?',
  'What do the benchmark numbers mean?',
  'How are answers cited?',
]

const GENERAL_SUGGESTIONS = [
  'What topics do these documents cover?',
  'Summarize the most important ideas.',
  'Which passages explain how search or retrieval works?',
  'What should I read first?',
]

function AnswerSkeleton({
  title = 'Retrieving and composing',
  label = 'Generating an answer',
}: {
  title?: string
  label?: string
}) {
  return (
    <div className="answer-skeleton" role="status" aria-busy="true" aria-label={label}>
      <div className="answer-skeleton__head">
        <span className="spinner" />
        <div>
          <strong>{title}</strong>
          <span>Embedding the question, searching HNSW, then generating an answer.</span>
        </div>
      </div>
      <Skeleton className="skeleton--line skeleton--w-90" />
      <Skeleton className="skeleton--line skeleton--w-75" />
      <Skeleton className="skeleton--line skeleton--w-80" />
      <Skeleton className="skeleton--line skeleton--w-45" />
      <div className="answer-skeleton__steps">
        <span className="pipeline-step is-active">embed</span>
        <span className="pipeline-step">search</span>
        <span className="pipeline-step">generate</span>
      </div>
    </div>
  )
}

export function AskPage({
  onOpenMap,
  onOpenDocuments,
}: {
  onOpenMap: () => void
  onOpenDocuments: () => void
}) {
  const { documents, lastAnswer, lastQuestion, setLastAnswer, loading, error: loadError, refreshAll } =
    useData()
  const { pushToast } = useToast()
  const [question, setQuestion] = useState(lastQuestion)
  const [k, setK] = useState(5)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const resultRef = useRef<HTMLDivElement>(null)
  const busyRef = useRef(false)

  const ask = async () => {
    if (busyRef.current) {
      return
    }
    const trimmed = question.trim()
    if (!trimmed) {
      setError('Write a question first.')
      return
    }
    if (!documents.length) {
      setError('Add a document before asking a question.')
      return
    }

    busyRef.current = true
    setBusy(true)
    setError(null)
    try {
      const response = await api.ask(trimmed, k)
      setLastAnswer(response, trimmed)
      window.requestAnimationFrame(() => {
        const result = resultRef.current
        result?.scrollIntoView({ behavior: scrollBehavior(), block: 'start' })
        // Move focus to the answer heading so screen-reader users hear that the
        // request finished instead of staying on the submit button.
        result?.querySelector<HTMLElement>('#answer-heading')?.focus({ preventScroll: true })
      })
    } catch (requestError) {
      const message =
        requestError instanceof Error ? requestError.message : 'The question could not be answered.'
      setError(message)
      // A previous answer belongs to a different question; keeping it on screen
      // after a failure would suggest the new question had been answered.
      setLastAnswer(null)
      pushToast({ tone: 'error', title: 'Question failed', message })
    } finally {
      busyRef.current = false
      setBusy(false)
    }
  }

  const clear = () => {
    setLastAnswer(null)
    setQuestion('')
    setError(null)
  }

  return (
    <div className="page">
      <PageHeader
        eyebrow="Retrieval"
        title="Ask your knowledge base"
        description="Questions are embedded, matched against the HNSW index, and answered from the retrieved chunks. Answers cite their sources, and every retrieved passage is shown below for checking."
        actions={
          lastAnswer ? (
            <Button variant="ghost" onClick={clear} icon={<Icon name="close" size={15} />}>
              Clear answer
            </Button>
          ) : undefined
        }
      />

      <AskComposer
        question={question}
        onQuestionChange={setQuestion}
        k={k}
        onKChange={setK}
        busy={busy}
        error={error}
        documentCount={documents.length}
        onSubmit={() => void ask()}
      />

      {loadError && documents.length === 0 ? (
        <ErrorNotice message={loadError} retry={() => void refreshAll()} />
      ) : loading && documents.length === 0 ? (
        <AnswerSkeleton
          title="Loading your library"
          label="Loading documents before asking a question"
        />
      ) : !loading && documents.length === 0 ? (
        <EmptyState
          icon="documents"
          title="Your library is empty"
          description="Add a document first. HNSWhy needs a corpus before it can retrieve relevant passages."
          action={
            <Button variant="primary" onClick={onOpenDocuments} icon={<Icon name="plus" size={16} />}>
              Add documents
            </Button>
          }
        />
      ) : busy ? (
        <AnswerSkeleton />
      ) : lastAnswer ? (
        <div ref={resultRef} className="answer-result">
          <div className="answer-result__question">
            <span className="eyebrow">Latest question</span>
            <p>{lastQuestion}</p>
          </div>
          <AnswerView answer={lastAnswer} onOpenMap={onOpenMap} />
        </div>
      ) : (
        <section className="ask-empty" aria-labelledby="suggestions-heading">
          <div className="ask-empty__intro">
            <span className="ask-empty__icon">
              <Icon name="quote" size={22} />
            </span>
            <h2 id="suggestions-heading">Start with a question</h2>
            <p>
              Ask about a concept in your documents. The answer will cite the exact chunks it used,
              and you can inspect every retrieved passage below it.
            </p>
          </div>
          <div className="suggestion-grid">
            {(documents.some((document) => document.source === 'sample')
              ? SAMPLE_SUGGESTIONS
              : GENERAL_SUGGESTIONS
            ).map((suggestion) => (
              <button
                key={suggestion}
                type="button"
                className="suggestion"
                onClick={() => setQuestion(suggestion)}
                disabled={busy}
              >
                <span>{suggestion}</span>
                <Icon name="arrowUpRight" size={15} />
              </button>
            ))}
          </div>
          <p className="ask-empty__note">
            Offline mode uses an extractive answerer. Configure Ollama or an OpenAI-compatible model
            for natural-language answers — retrieval and citations stay the same.
          </p>
        </section>
      )}
    </div>
  )
}
