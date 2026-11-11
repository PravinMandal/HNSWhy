import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from 'react'
import { api } from '../api/client'
import type {
  AskResponse,
  DocumentSummary,
  HealthResponse,
  StatusResponse,
} from '../api/types'

interface DataValue {
  status: StatusResponse | null
  health: HealthResponse | null
  documents: DocumentSummary[]
  loading: boolean
  refreshing: boolean
  error: string | null
  refreshAll: () => Promise<boolean>
  lastAnswer: AskResponse | null
  lastQuestion: string
  setLastAnswer: (answer: AskResponse | null, question?: string) => void
}

const DataContext = createContext<DataValue | null>(null)
const LAST_ANSWER_KEY = 'hnswhy-last-answer'

interface StoredAnswer {
  answer: AskResponse
  question: string
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null
}

function isFiniteNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value)
}

function isAskSource(value: unknown): boolean {
  return (
    isRecord(value) &&
    isFiniteNumber(value.chunk_id) &&
    isFiniteNumber(value.document_id) &&
    typeof value.title === 'string' &&
    isFiniteNumber(value.ordinal) &&
    typeof value.text === 'string' &&
    isFiniteNumber(value.distance)
  )
}

// AskPage renders these fields directly (`source.distance.toFixed(2)`,
// `timings_ms.total`), so a stored answer with missing or malformed nested
// fields would crash the whole app rather than show a recoverable error.
function isAskResponse(value: unknown): value is AskResponse {
  if (!isRecord(value)) {
    return false
  }
  const candidate = value as Partial<AskResponse>
  const timings = candidate.timings_ms
  const models = candidate.models
  return (
    typeof candidate.answer === 'string' &&
    Array.isArray(candidate.sources) &&
    candidate.sources.every(isAskSource) &&
    Array.isArray(candidate.query_vector) &&
    candidate.query_vector.every(isFiniteNumber) &&
    isRecord(timings) &&
    isFiniteNumber(timings.embedding) &&
    isFiniteNumber(timings.search) &&
    isFiniteNumber(timings.generate) &&
    isFiniteNumber(timings.total) &&
    isRecord(models) &&
    typeof models.embedding_provider === 'string' &&
    typeof models.embedding_model === 'string' &&
    typeof models.chat_provider === 'string' &&
    typeof models.chat_model === 'string'
  )
}

function readStoredAnswer(): StoredAnswer | null {
  try {
    const raw = sessionStorage.getItem(LAST_ANSWER_KEY)
    if (!raw) {
      return null
    }
    const parsed = JSON.parse(raw) as unknown
    if (!parsed || typeof parsed !== 'object') {
      return null
    }
    const record = parsed as Record<string, unknown>
    const answer = record.answer ?? record
    if (!isAskResponse(answer)) {
      return null
    }
    return {
      answer,
      question: typeof record.question === 'string' ? record.question : '',
    }
  } catch {
    return null
  }
}

function describeFailure(reason: unknown): string {
  return reason instanceof Error ? reason.message : 'Could not reach the HNSWhy server.'
}

export function DataProvider({ children }: { children: ReactNode }) {
  const [status, setStatus] = useState<StatusResponse | null>(null)
  const [health, setHealth] = useState<HealthResponse | null>(null)
  const [documents, setDocuments] = useState<DocumentSummary[]>([])
  const [loading, setLoading] = useState(true)
  const [refreshing, setRefreshing] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [storedAnswer, setStoredAnswer] = useState<StoredAnswer | null>(readStoredAnswer)
  // Only the newest refresh may publish state. A slow pre-upload refresh must
  // not land after the post-upload one and restore a stale document list.
  const refreshRequest = useRef(0)

  const refreshAll = useCallback(async (): Promise<boolean> => {
    const request = refreshRequest.current + 1
    refreshRequest.current = request
    setRefreshing(true)
    const [statusResult, healthResult, documentsResult] = await Promise.allSettled([
      api.status(),
      api.health(),
      api.documents(),
    ])

    let failure: unknown = null
    if (statusResult.status === 'fulfilled') {
      if (request === refreshRequest.current) {
        setStatus(statusResult.value)
      }
    } else {
      failure = statusResult.reason
    }
    if (healthResult.status === 'fulfilled') {
      if (request === refreshRequest.current) {
        setHealth(healthResult.value)
      }
    } else {
      failure ??= healthResult.reason
    }
    if (documentsResult.status === 'fulfilled') {
      if (request === refreshRequest.current) {
        setDocuments(documentsResult.value)
      }
    } else {
      failure ??= documentsResult.reason
    }

    if (request === refreshRequest.current) {
      setError(failure === null ? null : describeFailure(failure))
      setLoading(false)
      setRefreshing(false)
    }
    return failure === null
  }, [])

  useEffect(() => {
    void refreshAll()
  }, [refreshAll])

  const setLastAnswer = useCallback((answer: AskResponse | null, question = '') => {
    if (!answer) {
      setStoredAnswer(null)
      try {
        sessionStorage.removeItem(LAST_ANSWER_KEY)
      } catch {
        // Storage can be unavailable; the current tab still clears normally.
      }
      return
    }

    const next = { answer, question }
    setStoredAnswer(next)
    try {
      sessionStorage.setItem(LAST_ANSWER_KEY, JSON.stringify(next))
    } catch {
      // A large answer can exceed the quota. The in-memory value still keeps
      // the map overlay for this tab.
    }
  }, [])

  const value = useMemo<DataValue>(
    () => ({
      status,
      health,
      documents,
      loading,
      refreshing,
      error,
      refreshAll,
      lastAnswer: storedAnswer?.answer ?? null,
      lastQuestion: storedAnswer?.question ?? '',
      setLastAnswer,
    }),
    [
      status,
      health,
      documents,
      loading,
      refreshing,
      error,
      refreshAll,
      storedAnswer,
      setLastAnswer,
    ],
  )

  return <DataContext.Provider value={value}>{children}</DataContext.Provider>
}

export function useData(): DataValue {
  const value = useContext(DataContext)
  if (!value) {
    throw new Error('useData must be used inside DataProvider')
  }
  return value
}
