import { createPortal } from 'react-dom'
import { useToast } from '../app/ToastProvider'
import { Icon } from './Icon'

const toneIcon = {
  success: 'check',
  error: 'alert',
  info: 'info',
} as const

export function ToastViewport() {
  const { toasts, dismissToast } = useToast()

  // The application root is marked inert while a modal is open. Portaling the
  // viewport to <body> keeps upload errors and confirmations announceable.
  return createPortal(
    <div className="toast-viewport" aria-live="polite" aria-atomic="false">
      {toasts.map((toast) => (
        <div
          key={toast.id}
          className={`toast toast--${toast.tone}`}
          role={toast.tone === 'error' ? 'alert' : 'status'}
          aria-atomic="true"
        >
          <span className="toast__icon">
            <Icon name={toneIcon[toast.tone]} size={16} />
          </span>
          <div className="toast__copy">
            <strong>{toast.title}</strong>
            {toast.message ? <p>{toast.message}</p> : null}
          </div>
          <button
            type="button"
            className="toast__close"
            onClick={() => dismissToast(toast.id)}
            aria-label="Dismiss notification"
          >
            <Icon name="close" size={14} />
          </button>
        </div>
      ))}
    </div>,
    document.body,
  )
}
