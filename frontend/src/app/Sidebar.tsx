import type { RefObject } from 'react'
import { useData } from '../app/DataProvider'
import { useTheme } from '../app/ThemeProvider'
import { useToast } from '../app/ToastProvider'
import { routeLabels, type Route } from '../app/router'
import { formatCompact } from '../lib/format'
import { Icon, type IconName } from '../ui/Icon'

const navItems: Array<{
  route: Route
  label: string
  hint: string
  icon: IconName
}> = [
  { route: 'documents', label: 'Documents', hint: 'Build the library', icon: 'documents' },
  { route: 'ask', label: 'Ask', hint: 'Question the corpus', icon: 'sparkles' },
  { route: 'map', label: 'Map', hint: 'Explore embedding space', icon: 'map' },
  { route: 'benchmark', label: 'Benchmark', hint: 'Measured engine results', icon: 'chart' },
]

function BrandMark() {
  return (
    <svg viewBox="0 0 40 40" className="brand-mark" aria-hidden="true">
      <rect width="40" height="40" rx="11" fill="currentColor" opacity=".08" />
      <path
        d="M12 27 18 14M22 13l8-2M13 29l14 2M27 28l3-13"
        stroke="currentColor"
        strokeWidth="1.5"
        strokeLinecap="round"
        opacity=".38"
      />
      <circle cx="11" cy="28" r="2.7" fill="currentColor" />
      <circle cx="26" cy="30" r="2.7" fill="currentColor" />
      <circle cx="18" cy="13" r="3.1" fill="var(--accent)" />
      <circle cx="31" cy="11" r="2.1" fill="currentColor" opacity=".65" />
    </svg>
  )
}

function ConnectionStatus() {
  const { health, status, loading } = useData()
  const online = health?.status === 'ok'
  const degraded = health?.status === 'degraded'
  const connecting = loading && !health && !status
  const tone = connecting ? 'pending' : online ? 'ok' : degraded ? 'warn' : 'error'
  const label = connecting
    ? 'Connecting'
    : online
      ? 'Index online'
      : degraded
        ? 'Providers degraded'
        : 'API offline'

  return (
    <div className="rail-status" aria-label={`Server status: ${label}`}>
      <div className="rail-status__top">
        <span className={`status-dot status-dot--${tone}`} />
        <strong>{label}</strong>
      </div>
      <span className="rail-status__line">
        {formatCompact(status?.index.live ?? 0)} live vectors · {status?.embedding.dim ?? 0}d
      </span>
      <span className="rail-status__line rail-status__line--muted">
        {status ? `${status.embedding.provider}/${status.embedding.model}` : 'Waiting for the server'}
      </span>
    </div>
  )
}

export function Sidebar({
  route,
  navigate,
  onClose,
  sidebarRef,
  closeButtonRef,
  inert,
}: {
  route: Route
  navigate: (route: Route) => void
  onClose: () => void
  sidebarRef: RefObject<HTMLElement | null>
  closeButtonRef: RefObject<HTMLButtonElement | null>
  inert?: boolean
}) {
  const { documents, refreshing, refreshAll } = useData()
  const { theme, toggle } = useTheme()
  const { pushToast } = useToast()

  const refresh = () => {
    void refreshAll().then((ok) => {
      if (!ok) {
        pushToast({
          tone: 'error',
          title: 'Refresh failed',
          message: 'The server could not be reached.',
        })
      }
    })
  }

  return (
    <aside
      ref={sidebarRef}
      id="primary-sidebar"
      className="rail"
      aria-label="Primary navigation"
      inert={inert}
    >
      <div className="rail__top">
        <button
          type="button"
          className="brand"
          onClick={() => navigate('documents')}
          aria-label="HNSWhy home"
        >
          <BrandMark />
          <span className="brand__copy">
            <strong>HNSWhy</strong>
            <small>HNSW knowledge engine</small>
          </span>
        </button>
        <button
          ref={closeButtonRef}
          type="button"
          className="rail__close"
          onClick={onClose}
          aria-label="Close navigation"
        >
          <Icon name="close" size={18} />
        </button>
      </div>

      <div className="rail__section">
        <span className="rail__label">Workspace</span>
        <nav className="rail-nav">
          {navItems.map((item) => {
            const active = route === item.route
            return (
              <button
                key={item.route}
                type="button"
                className={`rail-nav__item ${active ? 'is-active' : ''}`}
                onClick={() => navigate(item.route)}
                aria-current={active ? 'page' : undefined}
              >
                <span className="rail-nav__icon">
                  <Icon name={item.icon} size={18} />
                </span>
                <span className="rail-nav__copy">
                  <strong>{item.label}</strong>
                  <small>{item.hint}</small>
                </span>
                {item.route === 'documents' ? (
                  <span className="rail-nav__count">{documents.length}</span>
                ) : null}
              </button>
            )
          })}
        </nav>
      </div>

      <div className="rail__spacer" />
      <ConnectionStatus />
      <div className="rail__footer">
        <button
          type="button"
          className="rail-action"
          onClick={toggle}
          aria-label={`Switch to ${theme === 'dark' ? 'light' : 'dark'} mode`}
        >
          <Icon name={theme === 'dark' ? 'sun' : 'moon'} size={16} />
          <span>{theme === 'dark' ? 'Light mode' : 'Dark mode'}</span>
        </button>
        <button
          type="button"
          className="rail-action rail-action--icon"
          onClick={refresh}
          aria-label="Refresh server status"
          title="Refresh server status"
        >
          <Icon name="refresh" size={16} className={refreshing ? 'spin' : undefined} />
        </button>
      </div>
      <span className="rail__caption">{routeLabels[route]} · v1</span>
    </aside>
  )
}
