import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { useRef, useState } from 'react'
import { describe, expect, it, vi } from 'vitest'
import { ConfirmDialog, Modal } from './Modal'

describe('Modal', () => {
  it('labels the dialog and restores focus to the opener on close', async () => {
    const user = userEvent.setup()
    function Harness() {
      const [open, setOpen] = useState(false)
      const insideRef = useRef<HTMLButtonElement>(null)
      return (
        <>
          <button type="button" onClick={() => setOpen(true)}>
            Open
          </button>
          <Modal
            open={open}
            onClose={() => setOpen(false)}
            title="Add a document"
            initialFocusRef={insideRef}
          >
            <button ref={insideRef} type="button">
              Inside
            </button>
          </Modal>
        </>
      )
    }

    render(<Harness />)
    const opener = screen.getByRole('button', { name: 'Open' })
    await user.click(opener)

    expect(screen.getByRole('dialog', { name: 'Add a document' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Inside' })).toHaveFocus()

    await user.keyboard('{Escape}')
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument()
    expect(opener).toHaveFocus()
  })

  it('does not close on Escape while busy', async () => {
    const user = userEvent.setup()
    const onClose = vi.fn()
    render(
      <Modal open onClose={onClose} title="Working" busy>
        <p>Uploading</p>
      </Modal>,
    )

    await user.keyboard('{Escape}')
    expect(onClose).not.toHaveBeenCalled()
  })

  it('wires consequence copy as the accessible description', () => {
    render(
      <ConfirmDialog
        open
        onClose={vi.fn()}
        onConfirm={vi.fn()}
        title="Delete this document?"
        description="This removes its chunks and vectors."
        confirmLabel="Delete document"
      />,
    )

    const dialog = screen.getByRole('dialog', { name: 'Delete this document?' })
    expect(dialog).toHaveAccessibleDescription('This removes its chunks and vectors.')
  })
})
