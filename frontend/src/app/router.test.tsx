import { act, renderHook } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { useRoute } from './router'

function hashChanged(hash: string) {
  window.location.hash = hash
  window.dispatchEvent(new HashChangeEvent('hashchange'))
}

describe('useRoute', () => {
  afterEach(() => {
    window.location.hash = ''
    // The setup file installs a shared scrollTo mock; reset it between tests so
    // "was not called" assertions cannot see a previous test's call.
    vi.clearAllMocks()
  })

  it('reads a known route from the URL hash', () => {
    window.location.hash = '#/benchmark'
    const { result } = renderHook(() => useRoute())
    expect(result.current.route).toBe('benchmark')
  })

  it('keeps the current route for an in-page anchor such as the skip link', () => {
    window.location.hash = '#/ask'
    const { result } = renderHook(() => useRoute())

    act(() => hashChanged('#main-content'))

    expect(result.current.route).toBe('ask')
    expect(window.scrollTo).not.toHaveBeenCalled()
  })

  it('navigates to a known route and updates the hash', () => {
    window.location.hash = '#/documents'
    const { result } = renderHook(() => useRoute())

    act(() => result.current.navigate('map'))

    expect(result.current.route).toBe('map')
    expect(window.location.hash).toBe('#/map')
  })

  it('falls back to Documents for an unknown route', () => {
    window.location.hash = '#/does-not-exist'
    const { result } = renderHook(() => useRoute())
    expect(result.current.route).toBe('documents')
  })
})
