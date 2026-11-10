import { afterEach, describe, expect, it, vi } from 'vitest'
import { ApiError, api } from './client'

const summary = {
  id: 1,
  title: 'Notes',
  source: 'notes.md',
  chunk_count: 2,
  char_count: 120,
  created_at: '2026-11-10T14:04:58Z',
}

function jsonResponse(body: unknown, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json' },
  })
}

class FakeXhr {
  static instances: FakeXhr[] = []

  upload: { onprogress: ((event: ProgressEvent) => void) | null } = { onprogress: null }
  status = 0
  responseText = ''
  timeout = 0
  onerror: (() => void) | null = null
  ontimeout: (() => void) | null = null
  onload: (() => void) | null = null
  open = vi.fn()
  send = vi.fn()

  constructor() {
    FakeXhr.instances.push(this)
  }
}

function installFakeXhr() {
  FakeXhr.instances = []
  vi.stubGlobal('XMLHttpRequest', FakeXhr)
}

function latestXhr(): FakeXhr {
  const xhr = FakeXhr.instances.at(-1)
  if (!xhr) {
    throw new Error('upload did not construct an XMLHttpRequest')
  }
  return xhr
}

describe('api client', () => {
  afterEach(() => {
    vi.unstubAllGlobals()
    vi.useRealTimers()
  })

  it('parses a GET response without claiming a JSON request body', async () => {
    const fetchMock = vi.fn().mockResolvedValue(jsonResponse([summary]))
    vi.stubGlobal('fetch', fetchMock)

    await expect(api.documents()).resolves.toEqual([summary])

    const [url, init] = fetchMock.mock.calls[0] as [string, RequestInit]
    expect(url).toBe('/api/documents')
    expect(init.headers).toMatchObject({ Accept: 'application/json' })
    expect(init.headers).not.toHaveProperty('Content-Type')
    expect(init.signal).toBeInstanceOf(AbortSignal)
  })

  it('posts the question and k as JSON and returns the answer', async () => {
    const answer = { answer: 'Cited [1]', sources: [], query_vector: [0.1], timings_ms: {}, models: {} }
    const fetchMock = vi.fn().mockResolvedValue(jsonResponse(answer))
    vi.stubGlobal('fetch', fetchMock)

    await expect(api.ask('What is HNSW?', 5)).resolves.toEqual(answer)

    const [url, init] = fetchMock.mock.calls[0] as [string, RequestInit]
    expect(url).toBe('/api/ask')
    expect(init.method).toBe('POST')
    expect(init.headers).toMatchObject({
      Accept: 'application/json',
      'Content-Type': 'application/json',
    })
    expect(JSON.parse(String(init.body))).toEqual({ question: 'What is HNSW?', k: 5 })
  })

  it('uploads a file through XHR, forwards progress, and resolves the document', async () => {
    installFakeXhr()
    const progress: number[] = []
    const pending = api.upload(new File(['hello'], 'notes.md'), ' Notes ', (value) => progress.push(value))
    const xhr = latestXhr()

    expect(xhr.open).toHaveBeenCalledWith('POST', '/api/documents')
    expect(xhr.send).toHaveBeenCalledWith(expect.any(FormData))
    xhr.upload.onprogress?.({ lengthComputable: true, loaded: 5, total: 10 } as ProgressEvent)
    xhr.status = 201
    xhr.responseText = JSON.stringify({ document: summary })
    xhr.onload?.()

    await expect(pending).resolves.toEqual(summary)
    expect(progress).toEqual([50, 100])
  })

  it('surfaces a structured upload rejection', async () => {
    installFakeXhr()
    const pending = api.upload(new File(['x'], 'notes.md'), '', () => {})
    const xhr = latestXhr()
    xhr.status = 413
    xhr.responseText = JSON.stringify({
      error: { code: 'PAYLOAD_TOO_LARGE', message: 'That file is too large.' },
    })
    xhr.onload?.()

    await expect(pending).rejects.toMatchObject({
      message: 'That file is too large.',
      code: 'PAYLOAD_TOO_LARGE',
      status: 413,
    })
  })

  it('reports upload network, timeout, and unreadable-response failures', async () => {
    installFakeXhr()

    const network = api.upload(new File(['x'], 'a.txt'), '', () => {})
    latestXhr().onerror?.()
    await expect(network).rejects.toMatchObject({ code: 'NETWORK_ERROR' })

    const timeout = api.upload(new File(['x'], 'a.txt'), '', () => {})
    latestXhr().ontimeout?.()
    await expect(timeout).rejects.toMatchObject({ code: 'TIMEOUT' })

    const unreadable = api.upload(new File(['x'], 'a.txt'), '', () => {})
    const xhr = latestXhr()
    xhr.status = 201
    xhr.responseText = 'not json'
    xhr.onload?.()
    await expect(unreadable).rejects.toMatchObject({
      message: expect.stringContaining('unreadable upload response'),
    })
  })

  it('turns a structured API error into an ApiError with code and status', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(
        jsonResponse({ error: { code: 'INVALID_INPUT', message: 'Question is required.' } }, 400),
      ),
    )

    const error = await api.ask('', 5).catch((reason: unknown) => reason)

    expect(error).toBeInstanceOf(ApiError)
    expect(error).toMatchObject({
      message: 'Question is required.',
      code: 'INVALID_INPUT',
      status: 400,
    })
  })

  it('reports an unreachable server instead of a raw fetch failure', async () => {
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new TypeError('fetch failed')))

    await expect(api.status()).rejects.toMatchObject({
      code: 'NETWORK_ERROR',
      message: expect.stringContaining('not reachable'),
    })
  })

  it('rejects an unreadable success response', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('not json', { status: 200 })))

    await expect(api.health()).rejects.toMatchObject({ code: 'INVALID_RESPONSE', status: 200 })
  })

  it('aborts a request that exceeds the client timeout', async () => {
    vi.useFakeTimers()
    vi.stubGlobal(
      'fetch',
      vi.fn().mockImplementation(
        (_url: string, init: RequestInit) =>
          new Promise((_resolve, reject) => {
            init.signal?.addEventListener('abort', () =>
              reject(new DOMException('Aborted', 'AbortError')),
            )
          }),
      ),
    )

    const pending = expect(api.documents()).rejects.toMatchObject({ code: 'TIMEOUT' })
    await vi.advanceTimersByTimeAsync(30_000)
    await pending
  })
})
