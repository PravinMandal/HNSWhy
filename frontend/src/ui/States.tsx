import type { ReactNode } from 'react'
import { Button } from './Button'
import { Icon, type IconName } from './Icon'

export function Skeleton({ className = '' }: { className?: string }) {
  return <span className={`skeleton ${className}`.trim()} aria-hidden="true" />
}

export function EmptyState({
  icon,
  title,
  description,
  action,
  compact = false,
}: {
  icon: IconName
  title: string
  description: ReactNode
  action?: ReactNode
  compact?: boolean
}) {
  return (
    <div className={`empty ${compact ? 'empty--compact' : ''}`}>
      <span className="empty__icon">
        <Icon name={icon} size={compact ? 22 : 28} />
      </span>
      <h3>{title}</h3>
      <p>{description}</p>
      {action ? <div className="empty__action">{action}</div> : null}
    </div>
  )
}

export function ErrorNotice({
  message,
  retry,
}: {
  message: string
  retry?: () => void
}) {
  return (
    <div className="error-notice" role="alert">
      <span className="error-notice__icon">
        <Icon name="alert" size={18} />
      </span>
      <div className="error-notice__copy">
        <strong>Something went wrong</strong>
        <p>{message}</p>
      </div>
      {retry ? (
        <Button variant="quiet" size="sm" onClick={retry}>
          Try again
        </Button>
      ) : null}
    </div>
  )
}

export function InlineError({ children }: { children: ReactNode }) {
  return (
    <p className="inline-error" role="alert">
      <Icon name="alert" size={15} />
      {children}
    </p>
  )
}
