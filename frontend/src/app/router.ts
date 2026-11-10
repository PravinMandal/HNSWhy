import { useCallback, useEffect, useState } from 'react'
import { scrollBehavior } from '../lib/motion'

export const routes = ['documents', 'ask', 'map', 'benchmark'] as const
export type Route = (typeof routes)[number]

export const routeLabels: Record<Route, string> = {
  documents: 'Documents',
  ask: 'Ask',
  map: 'Map',
  benchmark: 'Benchmark',
}

// A fragment such as #main-content is an in-page anchor, not a route. Return
// null so the current route survives and the hashchange handler can ignore it.
function routeFromHash(): Route | null {
  const hash = window.location.hash
  if (!hash.startsWith('#/')) {
    return null
  }
  const raw = hash.slice(2).split('?')[0]
  return routes.includes(raw as Route) ? (raw as Route) : null
}

export function useRoute(): { route: Route; navigate: (route: Route) => void } {
  const [route, setRoute] = useState<Route>(() => routeFromHash() ?? 'documents')

  useEffect(() => {
    const onHashChange = () => {
      const next = routeFromHash()
      if (!next) {
        return
      }
      setRoute(next)
      window.scrollTo({ top: 0, behavior: scrollBehavior() })
    }
    window.addEventListener('hashchange', onHashChange)
    return () => window.removeEventListener('hashchange', onHashChange)
  }, [])

  const navigate = useCallback((next: Route) => {
    if (window.location.hash !== `#/${next}`) {
      window.location.hash = `#/${next}`
    }
    setRoute(next)
  }, [])

  return { route, navigate }
}
