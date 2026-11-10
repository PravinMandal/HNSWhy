import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { describe, expect, it } from 'vitest'
import { ToastProvider, useToast } from '../app/ToastProvider'
import { ToastViewport } from './ToastViewport'

function Trigger() {
  const { pushToast } = useToast()
  return (
    <button
      type="button"
      onClick={() => pushToast({ tone: 'error', title: 'Upload failed', message: 'Try again.' })}
    >
      Push
    </button>
  )
}

describe('ToastViewport', () => {
  it('portals alerts to the body so they escape an inert app root', async () => {
    const user = userEvent.setup()
    render(
      <ToastProvider>
        <Trigger />
        <ToastViewport />
      </ToastProvider>,
    )
    document.getElementById('root')?.setAttribute('inert', '')

    await user.click(screen.getByRole('button', { name: 'Push' }))

    const alert = screen.getByRole('alert')
    expect(alert).toHaveTextContent('Upload failed')
    // The portal target is a direct child of <body>, not inside #root.
    expect(alert.closest('#root')).toBeNull()
    document.getElementById('root')?.removeAttribute('inert')
  })
})
