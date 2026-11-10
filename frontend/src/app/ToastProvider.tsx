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

export type ToastTone = 'success' | 'error' | 'info'

export interface Toast {
  id: number
  tone: ToastTone
  title: string
  message?: string
}

type ToastInput = Omit<Toast, 'id'>

interface ToastValue {
  toasts: Toast[]
  pushToast: (toast: ToastInput) => void
  dismissToast: (id: number) => void
}

const ToastContext = createContext<ToastValue | null>(null)
const MAX_VISIBLE = 3
const LIFETIME_MS = 5200

export function ToastProvider({ children }: { children: ReactNode }) {
  const [toasts, setToasts] = useState<Toast[]>([])
  const nextId = useRef(0)
  const timers = useRef(new Map<number, number>())

  const dismissToast = useCallback((id: number) => {
    setToasts((current) => current.filter((toast) => toast.id !== id))
    const timer = timers.current.get(id)
    if (timer !== undefined) {
      window.clearTimeout(timer)
      timers.current.delete(id)
    }
  }, [])

  const pushToast = useCallback(
    (toast: ToastInput) => {
      nextId.current += 1
      const id = nextId.current
      setToasts((current) => [...current.slice(-(MAX_VISIBLE - 1)), { ...toast, id }])
      timers.current.set(
        id,
        window.setTimeout(() => dismissToast(id), LIFETIME_MS),
      )
    },
    [dismissToast],
  )

  useEffect(() => {
    const pending = timers.current
    return () => {
      for (const timer of pending.values()) {
        window.clearTimeout(timer)
      }
    }
  }, [])

  const value = useMemo(
    () => ({ toasts, pushToast, dismissToast }),
    [toasts, pushToast, dismissToast],
  )

  return <ToastContext.Provider value={value}>{children}</ToastContext.Provider>
}

export function useToast(): ToastValue {
  const value = useContext(ToastContext)
  if (!value) {
    throw new Error('useToast must be used inside ToastProvider')
  }
  return value
}
