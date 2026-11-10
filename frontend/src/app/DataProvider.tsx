import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
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

function isAskResponse(value: unknown): value is AskResponse {
  if (!value || typeof value !== 'object') {
    return false
  }
  const candidate = value as Partial<AskResponse>
  return (
    typeof candidate.answer === 'string' &&
    Array.isArray(candidate.sources) &&
    Array.isArray(candidate.query_vector) &&
    typeof candidate.timings_ms === 'object' &&
    candidate.timings_ms !== null &&
    typeof candidate.models === 'object' &&
    candidate.models !== null
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

  const refreshAll = useCallback(async (): Promise<boolean> => {
    setRefreshing(true)
    const [statusResult, healthResult, documentsResult] = await Promise.allSettled([
      api.status(),
      api.health(),
      api.documents(),
    ])

    let failure: unknown = null
    if (statusResult.status === 'fulfilled') {
      setStatus(statusResult.value)
    } else {
      failure = statusResult.reason
    }
    if (healthResult.status === 'fulfilled') {
      setHealth(healthResult.value)
    } else {
      failure ??= healthResult.reason
    }
    if (documentsResult.status === 'fulfilled') {
      setDocuments(documentsResult.value)
    } else {
      failure ??= documentsResult.reason
    }

    setError(failure === null ? null : describeFailure(failure))
    setLoading(false)
    setRefreshing(false)
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
