# End-to-end checks

These scripts exercise the shipped binary, not a mock:

- `run.sh` builds if needed, starts `hnswhy_server` on a fresh SQLite database,
  waits for `/api/health`, then runs both suites and stops the server.
- `api-e2e.sh` checks the HTTP surface: health, status, documents, detail, ask,
  map, benchmark, JSON and multipart uploads, PDF extraction, validation errors,
  deletes, and the single-writer database lock.
- `ui-e2e.cjs` drives the built React app in headless Chromium with Playwright:
  Documents (filter, detail dialog, paste upload, delete confirmation), Ask
  (answer, citations, sources), Map (2D points, inspector, query overlay, 3D
  canvas), Benchmark (report, efSearch switch, table), theme persistence,
  mobile drawer and overflow, skip link, focus trapping, and console/network
  errors. It also asserts that the real stylesheet is applied, so a broken
  static asset cannot pass as a working page.

## Run

```bash
# Build the release binaries and frontend first, or let run.sh do it.
scripts/e2e/run.sh

# A different port and a durable artifact directory:
PORT=8094 OUT=docs/results/e2e/latest scripts/e2e/run.sh
```

`OUT` receives `main.db`, `server.log`, `multipart.md`, `second-server.log`,
`ui-e2e.log`, and `screens/*.png`. The default is a fresh directory under
`$TMPDIR`; pass a project path when the run is release evidence.

The browser suite needs `playwright-core` and Chromium. It uses an explicit
`PLAYWRIGHT_CORE`, then a normal `node_modules` install, then the copy bundled
with the OpenCode desktop app. Override the browser with `CHROMIUM_PATH` if
needed:

```bash
npm install --no-save playwright-core
CHROMIUM_PATH=/usr/bin/chromium scripts/e2e/run.sh
```

The scripts are intentionally not part of `ctest` or `npm test`: they need a
live server, a built frontend, and a browser. They are the release gate that
runs after the fast suites.
