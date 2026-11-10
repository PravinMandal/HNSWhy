import type { ReactNode } from 'react'
import { Icon, type IconName } from './Icon'

export function PageHeader({
  eyebrow,
  title,
  description,
  actions,
}: {
  eyebrow: string
  title: string
  description: ReactNode
  actions?: ReactNode
}) {
  return (
    <header className="page-header">
      <div className="page-header__copy">
        <span className="eyebrow">{eyebrow}</span>
        <h1>{title}</h1>
        <p>{description}</p>
      </div>
      {actions ? <div className="page-header__actions">{actions}</div> : null}
    </header>
  )
}

export function SectionHeader({
  title,
  description,
  action,
  id,
}: {
  title: string
  description?: ReactNode
  action?: ReactNode
  id?: string
}) {
  return (
    <div className="section-header">
      <div>
        <h2 id={id}>{title}</h2>
        {description ? <p>{description}</p> : null}
      </div>
      {action ? <div className="section-header__action">{action}</div> : null}
    </div>
  )
}

export function MetricCard({
  icon,
  label,
  value,
  detail,
  tone = 'default',
}: {
  icon: IconName
  label: string
  value: ReactNode
  detail?: ReactNode
  tone?: 'default' | 'accent' | 'warn'
}) {
  return (
    <div className={`metric metric--${tone}`}>
      <span className="metric__icon">
        <Icon name={icon} size={17} />
      </span>
      <div className="metric__body">
        <span className="metric__label">{label}</span>
        <strong className="metric__value">{value}</strong>
        {detail ? <span className="metric__detail">{detail}</span> : null}
      </div>
    </div>
  )
}
