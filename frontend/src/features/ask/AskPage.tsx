import { useRef, useState } from 'react'
import { api } from '../../api/client'
import { useData } from '../../app/DataProvider'
import { useToast } from '../../app/ToastProvider'
import { Button } from '../../ui/Button'
import { EmptyState, ErrorNotice, Skeleton } from '../../ui/States'
import { Icon } from '../../ui/Icon'
import { PageHeader } from '../../ui/PageHeader'
import { AnswerView } from './AnswerView'
import { AskComposer } from './AskComposer'

const SUGGESTIONS = [
  'How does HNSW search work?',
  'How does HNSWhy keep SQLite and the index consistent?',
  'What do the benchmark numbers mean?',
  'How are answers grounded and cited?',
]

function AnswerSkeleton() {
  return (
    <div className="answer-skeleton" aria-busy="true" aria-label="Generating an answer">
      <div className="answer-skeleton__head">
        <span className="spinner" />
        <div>
          <strong>Retrieving and grounding</strong>
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
        resultRef.current?.scrollIntoView({ behavior: 'smooth', block: 'start' })
      })
    } catch (requestError) {
      const message =
        requestError instanceof Error ? requestError.message : 'The question could not be answered.'
      setError(message)
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
        description="Questions are embedded, matched against the HNSW index, and answered only from the retrieved chunks. Every claim carries a source number."
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
      ) : !loading && documents.length === 0 ? (
        <EmptyState
          icon="documents"
          title="Your library is empty"
          description="Add a document first. HNSWhy needs a corpus before it can retrieve grounded evidence."
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
            {SUGGESTIONS.map((suggestion) => (
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
