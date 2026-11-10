import type {
  AskResponse,
  BenchmarkResponse,
  DocumentDetail,
  DocumentSummary,
  HealthResponse,
  MapResponse,
  StatusResponse,
} from './types'

const API_BASE = (import.meta.env.VITE_API_BASE as string | undefined)?.replace(/\/$/, '') ?? ''

export class ApiError extends Error {
  readonly code: string
  readonly status: number

  constructor(message: string, code = 'REQUEST_FAILED', status = 0) {
    super(message)
    this.name = 'ApiError'
    this.code = code
    this.status = status
  }
}

interface ErrorBody {
  error?: {
    code?: string
    message?: string
  }
}

async function parseError(response: Response): Promise<ApiError> {
  const raw = await response.text()
  if (!raw) {
    return new ApiError(`Request failed with HTTP ${response.status}`, 'HTTP_ERROR', response.status)
  }
  try {
    const body = JSON.parse(raw) as ErrorBody
    return new ApiError(
      body.error?.message ?? `Request failed with HTTP ${response.status}`,
      body.error?.code ?? 'HTTP_ERROR',
      response.status,
    )
  } catch {
    return new ApiError(raw.slice(0, 240), 'HTTP_ERROR', response.status)
  }
}

async function request<T>(path: string, init?: RequestInit, timeoutMs = 30_000): Promise<T> {
  const controller = new AbortController()
  const timer = window.setTimeout(() => controller.abort(), timeoutMs)
  let response: Response
  try {
    response = await fetch(`${API_BASE}${path}`, {
      ...init,
      signal: controller.signal,
      headers: {
        Accept: 'application/json',
        ...(init?.body ? { 'Content-Type': 'application/json' } : {}),
        ...init?.headers,
      },
    })
  } catch {
    if (controller.signal.aborted) {
      throw new ApiError('The request timed out. Check the server and try again.', 'TIMEOUT')
    }
    throw new ApiError(
      'The HNSWhy server is not reachable. Check that it is running and try again.',
      'NETWORK_ERROR',
    )
  } finally {
    window.clearTimeout(timer)
  }

  if (!response.ok) {
    throw await parseError(response)
  }
  if (response.status === 204) {
    return undefined as T
  }
  try {
    return (await response.json()) as T
  } catch {
    throw new ApiError('The server returned an unreadable response.', 'INVALID_RESPONSE', response.status)
  }
}

function upload(
  file: File,
  title: string,
  onProgress: (progress: number) => void,
): Promise<DocumentSummary> {
  return new Promise((resolve, reject) => {
    const form = new FormData()
    form.append('file', file)
    if (title.trim()) {
      form.append('title', title.trim())
    }

    const xhr = new XMLHttpRequest()
    xhr.open('POST', `${API_BASE}/api/documents`)
    xhr.responseType = 'text'
    xhr.timeout = 120_000
    xhr.upload.onprogress = (event) => {
      if (event.lengthComputable) {
        onProgress(Math.round((event.loaded / event.total) * 100))
      }
    }
    xhr.onerror = () => reject(new ApiError('Upload failed. Check the server and try again.'))
    xhr.ontimeout = () =>
      reject(new ApiError('The upload timed out. Check the server and try again.', 'TIMEOUT'))
    xhr.onload = () => {
      let body: { document?: DocumentSummary; error?: { code?: string; message?: string } } = {}
      try {
        body = xhr.responseText ? JSON.parse(xhr.responseText) : {}
      } catch {
        reject(new ApiError('The server returned an unreadable upload response.'))
        return
      }
      if (xhr.status >= 200 && xhr.status < 300 && body.document) {
        onProgress(100)
        resolve(body.document)
      } else {
        reject(
          new ApiError(
            body.error?.message ?? `Upload failed with HTTP ${xhr.status}`,
            body.error?.code ?? 'UPLOAD_FAILED',
            xhr.status,
          ),
        )
      }
    }
    xhr.send(form)
  })
}

export const api = {
  health: () => request<HealthResponse>('/api/health'),
  status: () => request<StatusResponse>('/api/status'),
  documents: () => request<DocumentSummary[]>('/api/documents'),
  document: (id: number) => request<DocumentDetail>(`/api/documents/${id}`),
  deleteDocument: (id: number) =>
    request<{ deleted: boolean; id: number }>(`/api/documents/${id}`, { method: 'DELETE' }),
  addText: (text: string, title = '', source = 'paste') =>
    request<{ document: DocumentSummary }>('/api/documents', {
      method: 'POST',
      body: JSON.stringify({ text, title, source }),
    }),
  upload,
  ask: (question: string, k: number) =>
    request<AskResponse>(
      '/api/ask',
      {
        method: 'POST',
        body: JSON.stringify({ question, k }),
      },
      120_000,
    ),
  map: (limit = 1200) => request<MapResponse>(`/api/map?limit=${limit}`),
  benchmark: () => request<BenchmarkResponse>('/api/benchmark'),
}
