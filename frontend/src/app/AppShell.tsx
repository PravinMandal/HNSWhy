import { useEffect, useRef, useState, type ReactNode } from 'react'
import { scrollBehavior } from '../lib/motion'
import { routeLabels, type Route } from './router'
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

  // Closing the drawer on navigation and focusing the new heading have to
  // happen in this order: while the drawer is open the workspace is inert, so
  // focusing the heading immediately would be a no-op. The flag also tells the
  // drawer cleanup not to pull focus back to the menu button.
  const pendingRouteFocus = useRef(false)
  const firstRoute = useRef(true)

  useEffect(() => {
    document.title = `${routeLabels[route]} · HNSWhy`
    if (firstRoute.current) {
      firstRoute.current = false
      return
    }
    pendingRouteFocus.current = true
    setMobileOpen(false)
  }, [route])

  // SPA navigation replaces the whole page, so move focus to the new page
  // heading once the drawer is out of the way. Without this, keyboard and
  // screen reader users stay parked on the navigation they just activated.
  useEffect(() => {
    if (!pendingRouteFocus.current || mobileOpen) {
      return
    }
    pendingRouteFocus.current = false
    const main = document.getElementById('main-content')
    const heading = main?.querySelector<HTMLElement>('h1')
    if (heading) {
      heading.tabIndex = -1
      heading.focus({ preventScroll: true })
    } else {
      main?.focus({ preventScroll: true })
    }
    window.scrollTo({ top: 0, behavior: scrollBehavior() })
  }, [route, mobileOpen])

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
      // A route change is closing the drawer to show a new page; the focus
      // effect above owns focus in that case.
      if (pendingRouteFocus.current) {
        return
      }
      const restore =
        previousFocus && previousFocus !== document.body && !sidebar?.contains(previousFocus)
          ? previousFocus
          : menuButtonRef.current
      restore?.focus()
    }
  }, [isMobile, mobileOpen])

  return (
    <div className="shell">
      <a
        className="skip-link"
        href="#main-content"
        onClick={(event) => {
          // The router owns the hash. Treat this as an in-page jump instead of
          // letting #main-content become an unknown route.
          event.preventDefault()
          const main = document.getElementById('main-content')
          main?.focus()
          main?.scrollIntoView({ behavior: scrollBehavior(), block: 'start' })
        }}
      >
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
      <div className="workspace" inert={isMobile && mobileOpen}>
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
