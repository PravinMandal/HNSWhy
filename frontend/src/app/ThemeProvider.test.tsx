import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { ThemeProvider, useTheme } from './ThemeProvider'

function Probe() {
  const { theme, toggle } = useTheme()
  return (
    <div>
      <span data-testid="theme">{theme}</span>
      <button type="button" onClick={toggle}>
        Toggle theme
      </button>
    </div>
  )
}

describe('ThemeProvider', () => {
  afterEach(() => {
    vi.unstubAllGlobals()
  })

  it('applies the stored theme and persists a toggle', async () => {
    localStorage.setItem('hnswhy-theme', 'dark')
    const user = userEvent.setup()

    render(
      <ThemeProvider>
        <Probe />
      </ThemeProvider>,
    )

    expect(screen.getByTestId('theme')).toHaveTextContent('dark')
    await waitFor(() => expect(document.documentElement.dataset.theme).toBe('dark'))

    await user.click(screen.getByRole('button', { name: 'Toggle theme' }))

    expect(screen.getByTestId('theme')).toHaveTextContent('light')
    expect(localStorage.getItem('hnswhy-theme')).toBe('light')
    await waitFor(() => expect(document.documentElement.dataset.theme).toBe('light'))
  })

  it('uses the system preference when no theme is stored', () => {
    vi.stubGlobal(
      'matchMedia',
      (query: string) => ({
        matches: query.includes('dark'),
        media: query,
        onchange: null,
        addListener: () => undefined,
        removeListener: () => undefined,
        addEventListener: () => undefined,
        removeEventListener: () => undefined,
        dispatchEvent: () => false,
      }),
    )

    render(
      <ThemeProvider>
        <Probe />
      </ThemeProvider>,
    )

    expect(screen.getByTestId('theme')).toHaveTextContent('dark')
  })
})
