import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { useState, type RefObject } from 'react'
import { describe, expect, it, vi } from 'vitest'
import { AppShell } from './AppShell'
import type { Route } from './router'

// The shell's focus ordering is the behavior under test, so replace the two
// data-heavy chrome components with the smallest controls that expose the same
// refs and navigation callback.
vi.mock('./Topbar', () => ({
  Topbar: ({
    onOpenMenu,
    menuButtonRef,
  }: {
    onOpenMenu: () => void
    menuButtonRef: RefObject<HTMLButtonElement | null>
  }) => (
    <button ref={menuButtonRef} type="button" onClick={onOpenMenu}>
      Open navigation
    </button>
  ),
}))

vi.mock('./Sidebar', () => ({
  Sidebar: ({
    navigate,
    sidebarRef,
    closeButtonRef,
  }: {
    navigate: (route: Route) => void
    sidebarRef: RefObject<HTMLElement | null>
    closeButtonRef: RefObject<HTMLButtonElement | null>
  }) => (
    <aside ref={sidebarRef}>
      <button ref={closeButtonRef} type="button">
        Close drawer
      </button>
      <button type="button" onClick={() => navigate('ask')}>
        Ask
      </button>
    </aside>
  ),
}))

function setMobileViewport(matches: boolean) {
  Object.defineProperty(window, 'matchMedia', {
    writable: true,
    value: vi.fn().mockImplementation((query: string) => ({
      matches,
      media: query,
      onchange: null,
      addListener: vi.fn(),
      removeListener: vi.fn(),
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      dispatchEvent: vi.fn(),
    })),
  })
}

function Harness() {
  const [route, setRoute] = useState<Route>('documents')
  return (
    <AppShell route={route} navigate={setRoute}>
      <h1>{route}</h1>
    </AppShell>
  )
}

describe('AppShell', () => {
  it('focuses the new heading after navigation closes the mobile drawer', async () => {
    const user = userEvent.setup()
    setMobileViewport(true)
    render(<Harness />)

    await user.click(screen.getByRole('button', { name: 'Open navigation' }))
    expect(screen.getByRole('button', { name: 'Close drawer' })).toHaveFocus()

    await user.click(screen.getByRole('button', { name: 'Ask' }))

    await waitFor(() => expect(screen.getByRole('heading', { name: 'ask' })).toHaveFocus())
    expect(document.title).toBe('Ask · HNSWhy')
  })
})
