import { useEffect, useRef, useState, type ReactNode } from 'react'
import type { Route } from './router'
import { Sidebar } from './Sidebar'
import { Topbar } from './Topbar'

const MOBILE_QUERY = '(max-width: 900px)'

export function AppShell({
  route,
  navigate,
  children,
}: {
  route: Route
  navigate: (route: Route) => void
  children: ReactNode
}) {
  const [mobileOpen, setMobileOpen] = useState(false)
  const [isMobile, setIsMobile] = useState(
    () => typeof window !== 'undefined' && window.matchMedia(MOBILE_QUERY).matches,
  )
  const sidebarRef = useRef<HTMLElement>(null)
  const menuButtonRef = useRef<HTMLButtonElement>(null)
  const closeButtonRef = useRef<HTMLButtonElement>(null)

  useEffect(() => {
    const media = window.matchMedia(MOBILE_QUERY)
    const onChange = () => setIsMobile(media.matches)
    media.addEventListener('change', onChange)
    return () => media.removeEventListener('change', onChange)
  }, [])

  useEffect(() => {
    setMobileOpen(false)
  }, [route])

  useEffect(() => {
    if (!isMobile || !mobileOpen) {
      return
    }

    const sidebar = sidebarRef.current
    const previousFocus = document.activeElement as HTMLElement | null
    closeButtonRef.current?.focus()

    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        event.preventDefault()
        setMobileOpen(false)
        return
      }
      if (event.key !== 'Tab' || !sidebar) {
        return
      }
      const focusable = Array.from(
        sidebar.querySelectorAll<HTMLElement>(
          'button:not([disabled]), [href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])',
        ),
      ).filter((element) => element.offsetParent !== null)
      if (focusable.length === 0) {
        return
      }
      const first = focusable[0]
      const last = focusable[focusable.length - 1]
      if (event.shiftKey && document.activeElement === first) {
        event.preventDefault()
        last.focus()
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault()
        first.focus()
      }
    }

    document.addEventListener('keydown', onKeyDown)
    return () => {
      document.removeEventListener('keydown', onKeyDown)
      const restore =
        previousFocus && previousFocus !== document.body && !sidebar?.contains(previousFocus)
          ? previousFocus
          : menuButtonRef.current
      restore?.focus()
    }
  }, [isMobile, mobileOpen])

  return (
    <div className="shell">
      <a className="skip-link" href="#main-content">
        Skip to content
      </a>
      <Sidebar
        route={route}
        navigate={navigate}
        onClose={() => setMobileOpen(false)}
        sidebarRef={sidebarRef}
        closeButtonRef={closeButtonRef}
        inert={isMobile && !mobileOpen}
      />
      {mobileOpen ? (
        <button
          type="button"
          className="rail-scrim"
          onClick={() => setMobileOpen(false)}
          aria-label="Close navigation"
        />
      ) : null}
      <div className="workspace">
        <Topbar
          route={route}
          onOpenMenu={() => setMobileOpen(true)}
          menuButtonRef={menuButtonRef}
          mobileOpen={mobileOpen}
        />
        <main id="main-content" className="main" tabIndex={-1}>
          {children}
        </main>
      </div>
    </div>
  )
}
