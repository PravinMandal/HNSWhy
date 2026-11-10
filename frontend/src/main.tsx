import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import '@fontsource-variable/inter/index.css'
import '@fontsource-variable/fraunces/index.css'
import '@fontsource-variable/jetbrains-mono/index.css'
import './styles/tokens.css'
import './styles/base.css'
import './styles/layout.css'
import './styles/ui.css'
import './styles/documents.css'
import './styles/ask.css'
import './styles/map.css'
import './styles/benchmark.css'
import { App } from './App'

const container = document.getElementById('root')
if (!container) {
  throw new Error('Root element not found')
}

createRoot(container).render(
  <StrictMode>
    <App />
  </StrictMode>,
)
