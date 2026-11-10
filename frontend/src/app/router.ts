import { useCallback, useEffect, useState } from 'react'

export const routes = ['documents', 'ask', 'map', 'benchmark'] as const
export type Route = (typeof routes)[number]

export const routeLabels: Record<Route, string> = {
  documents: 'Documents',
  ask: 'Ask',
  map: 'Map',
  benchmark: 'Benchmark',
}

function routeFromHash(): Route {
  const raw = window.location.hash.replace(/^#\/?/, '').split('?')[0]
  return routes.includes(raw as Route) ? (raw as Route) : 'documents'
}

export function useRoute(): { route: Route; navigate: (route: Route) => void } {
  const [route, setRoute] = useState<Route>(routeFromHash)

  useEffect(() => {
    const onHashChange = () => {
      setRoute(routeFromHash())
      window.scrollTo({ top: 0, behavior: 'smooth' })
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
