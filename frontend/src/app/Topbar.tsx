import type { RefObject } from 'react'
import { useData } from '../app/DataProvider'
import { useTheme } from '../app/ThemeProvider'
import { routeLabels, type Route } from '../app/router'
import { pluralize } from '../lib/format'
import { Icon } from '../ui/Icon'

export function Topbar({
  route,
  onOpenMenu,
  menuButtonRef,
  mobileOpen,
}: {
  route: Route
  onOpenMenu: () => void
  menuButtonRef: RefObject<HTMLButtonElement | null>
  mobileOpen: boolean
}) {
  const { health, documents, loading, status, error } = useData()
  const { theme, toggle } = useTheme()
  const online = health?.status === 'ok'
  const degraded = health?.status === 'degraded'
  const connecting = loading && !health && !status
  // A failed refresh leaves the last successful payload in state. Surface that
  // as a warning instead of continuing to claim the API is online.
  const stale = Boolean(error) && !loading
  const tone = connecting ? 'pending' : stale ? 'warn' : online ? 'ok' : degraded ? 'warn' : 'error'

  return (
    <header className="topbar">
      <div className="topbar__left">
        <button
          ref={menuButtonRef}
          type="button"
          className="topbar__menu"
          onClick={onOpenMenu}
          aria-label="Open navigation"
          aria-expanded={mobileOpen}
          aria-controls="primary-sidebar"
        >
          <Icon name="menu" size={19} />
        </button>
        <nav className="breadcrumb" aria-label="Breadcrumb">
          <span>Workspace</span>
          <Icon name="chevronDown" size={13} />
          <strong>{routeLabels[route]}</strong>
        </nav>
      </div>

      <div className="topbar__right">
        <span className={`health-pill health-pill--${tone}`}>
          <span className={`status-dot status-dot--${tone}`} />
          {connecting
            ? 'Connecting'
            : stale
              ? 'Reconnecting'
              : online
                ? 'API online'
                : degraded
                  ? 'Degraded'
                  : 'Offline'}
        </span>
        <span className="topbar__rule" aria-hidden="true" />
        <span className="topbar__documents">
          <Icon name="database" size={14} />
          {loading && !status ? 'Loading documents' : pluralize(documents.length, 'document')}
        </span>
        <button
          type="button"
          className="topbar__theme"
          onClick={toggle}
          aria-label={`Switch to ${theme === 'dark' ? 'light' : 'dark'} mode`}
        >
          <Icon name={theme === 'dark' ? 'sun' : 'moon'} size={17} />
        </button>
      </div>
    </header>
  )
}
