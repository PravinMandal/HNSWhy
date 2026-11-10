import { AppShell } from './app/AppShell'
import { DataProvider } from './app/DataProvider'
import { ThemeProvider } from './app/ThemeProvider'
import { ToastProvider } from './app/ToastProvider'
import { useRoute } from './app/router'
import { AskPage } from './features/ask/AskPage'
import { BenchmarkPage } from './features/benchmark/BenchmarkPage'
import { DocumentsPage } from './features/documents/DocumentsPage'
import { MapPage } from './features/map/MapPage'
import { ToastViewport } from './ui/ToastViewport'

export function App() {
  const { route, navigate } = useRoute()

  return (
    <ThemeProvider>
      <ToastProvider>
        <DataProvider>
          <AppShell route={route} navigate={navigate}>
            {route === 'documents' ? <DocumentsPage /> : null}
            {route === 'ask' ? (
              <AskPage
                onOpenMap={() => navigate('map')}
                onOpenDocuments={() => navigate('documents')}
              />
            ) : null}
            {route === 'map' ? <MapPage onOpenDocuments={() => navigate('documents')} /> : null}
            {route === 'benchmark' ? <BenchmarkPage /> : null}
          </AppShell>
        </DataProvider>
        <ToastViewport />
      </ToastProvider>
    </ThemeProvider>
  )
}
