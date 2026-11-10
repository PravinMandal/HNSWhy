#!/usr/bin/env node
/*
 * End-to-end UI checks for the built HNSWhy frontend.
 *
 * The script talks to a real backend through a real Chromium, so it covers the
 * same bytes a user receives: the Vite bundle in frontend/dist, the HTTP API,
 * SQLite, and the in-memory HNSW index. It deliberately uses accessible roles
 * and labels instead of CSS classes so a passing run also proves the controls
 * remain keyboard- and screen-reader-addressable.
 */
const fs = require('node:fs')
const path = require('node:path')

// Playwright is not a product dependency. Prefer an explicit path, then a
// normal node_modules install, then the copy that ships with the OpenCode
// desktop app. This keeps the check runnable without adding a browser stack to
// every `npm install`.
function loadPlaywright() {
  const candidates = [
    process.env.PLAYWRIGHT_CORE,
    'playwright-core',
    path.join(process.env.HOME || '', '.config/opencode/node_modules/playwright-core'),
  ].filter(Boolean)
  let lastError = null
  for (const candidate of candidates) {
    try {
      return require(candidate)
    } catch (error) {
      lastError = error
    }
  }
  throw new Error(
    `playwright-core not found. Install it with "npm install --no-save playwright-core" ` +
      `or set PLAYWRIGHT_CORE. Last error: ${lastError?.message}`,
  )
}

function findChromium() {
  if (process.env.CHROMIUM_PATH) {
    return process.env.CHROMIUM_PATH
  }
  const candidates = [
    '/usr/lib/chromium/chromium',
    '/usr/bin/chromium',
    '/usr/bin/chromium-browser',
    '/usr/bin/google-chrome',
    '/usr/bin/google-chrome-stable',
  ]
  const found = candidates.find((candidate) => fs.existsSync(candidate))
  if (!found) {
    throw new Error('Chromium not found. Set CHROMIUM_PATH to a Chromium or Chrome binary.')
  }
  return found
}

const { chromium } = loadPlaywright()

const BASE = process.env.BASE || 'http://127.0.0.1:8092'
const OUT = process.env.OUT || '/tmp/opencode/hnswhy-final-e2e'
const SHOTS = path.join(OUT, 'screens')

let passed = 0
let failed = 0
const failures = []

function assert(condition, message) {
  if (!condition) {
    throw new Error(message)
  }
}

async function check(name, fn) {
  try {
    await fn()
    console.log(`[PASS] ${name}`)
    passed += 1
  } catch (error) {
    console.log(`[FAIL] ${name}: ${error.message}`)
    failures.push({ name, message: error.message })
    failed += 1
  }
}

async function main() {
  fs.mkdirSync(SHOTS, { recursive: true })

  const browser = await chromium.launch({
    executablePath: findChromium(),
    headless: true,
    args: ['--no-sandbox', '--disable-gpu', '--disable-dev-shm-usage'],
  })
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } })
  const page = await context.newPage()

  const consoleErrors = []
  const pageErrors = []
  const failedRequests = []
  const badResponses = []

  page.on('console', (message) => {
    if (message.type() === 'error') {
      consoleErrors.push(message.text())
    }
  })
  page.on('pageerror', (error) => pageErrors.push(error.message))
  page.on('requestfailed', (request) => {
    // Aborted image or prefetch requests are not user-visible failures, but
    // every API and document request must complete.
    if (request.resourceType() !== 'image') {
      failedRequests.push(`${request.method()} ${request.url()} ${request.failure()?.errorText}`)
    }
  })
  page.on('response', (response) => {
    if (response.status() >= 400) {
      badResponses.push(`${response.status()} ${response.request().method()} ${response.url()}`)
    }
  })

  const metric = async (label) =>
    page
      .locator('.metric')
      .filter({ has: page.locator('.metric__label', { hasText: label }) })
      .locator('.metric__value')
      .innerText()

  const intOf = (text) => Number(text.replace(/[^0-9]/g, ''))

  await page.goto(`${BASE}/`, { waitUntil: 'domcontentloaded' })

  await check('styles are applied to the real build', async () => {
    const styles = await page.evaluate(() => ({
      hrefs: Array.from(document.styleSheets).map((sheet) => sheet.href),
      background: getComputedStyle(document.body).backgroundColor,
      shell: getComputedStyle(document.querySelector('.shell') ?? document.body).display,
    }))
    assert(styles.hrefs.length > 0, `no stylesheet loaded on ${page.url()}`)
    assert(
      styles.background === 'rgb(244, 241, 234)' || styles.background === 'rgb(20, 19, 17)',
      `unexpected body background ${styles.background} on ${page.url()}`,
    )
    assert(styles.shell === 'flex', `layout shell is ${styles.shell}, not flex`)
  })

  await check('documents page renders with a live server', async () => {
    await page.getByRole('heading', { name: 'Documents', level: 1 }).waitFor()
    await page.locator('.rail-status').filter({ hasText: 'Index online' }).waitFor()
  })

  await check('metric cards are populated, not placeholders', async () => {
    await page.locator('.metric__value').first().waitFor()
    const documents = await metric('Documents')
    const chunks = await metric('Chunks')
    const live = await metric('Live vectors')
    const dimensions = await metric('Dimensions')
    assert(intOf(documents) >= 8, `expected at least 8 documents, got ${documents}`)
    assert(intOf(chunks) > 0, `expected chunks > 0, got ${chunks}`)
    assert(intOf(live) > 0, `expected live vectors > 0, got ${live}`)
    assert(intOf(dimensions) === 256, `expected 256 dimensions, got ${dimensions}`)
  })

  await check('library lists the starter corpus and filters by title', async () => {
    const rows = page.locator('.document-row')
    const initial = await rows.count()
    assert(initial >= 8, `expected at least 8 library rows, got ${initial}`)
    const search = page.getByPlaceholder('Filter by title or source…')
    await search.fill('benchmark')
    await page.waitForTimeout(150)
    const filtered = await rows.count()
    assert(filtered > 0, 'expected at least one document to match "benchmark"')
    assert(filtered < initial, `filter should narrow ${initial} rows, still ${filtered}`)
    await search.fill('')
    await page.waitForTimeout(150)
    assert((await rows.count()) === initial, 'clearing the filter should restore every row')
  })

  await check('document detail dialog opens with text and chunk tabs', async () => {
    await page.getByRole('button', { name: 'Open latest' }).click()
    const dialog = page.getByRole('dialog')
    await dialog.waitFor()
    await dialog.getByRole('tab', { name: /text/i }).waitFor()
    await dialog.getByRole('tab', { name: /chunks/i }).click()
    await dialog.locator('.chunk-list, .chunk-item, [class*="chunk"]').first().waitFor()
    await dialog.getByRole('button', { name: 'Close dialog' }).click()
    await dialog.waitFor({ state: 'hidden' })
  })

  await check('pasted text becomes a searchable document', async () => {
    await page.getByRole('button', { name: 'Add documents' }).click()
    const dialog = page.getByRole('dialog')
    await dialog.waitFor()
    await dialog.getByRole('button', { name: 'Paste text' }).click()
    await dialog.getByLabel(/Title/).fill('E2E pasted note')
    await dialog.getByLabel('Source label').fill('e2e')
    await dialog
      .getByLabel('Text')
      .fill(
        'The HNSWhy end-to-end test stores this exact marker: zebra-quartz-4417. ' +
          'SQLite keeps the canonical rows while the HNSW graph serves nearest neighbours.',
      )
    await dialog.getByRole('button', { name: 'Add and index' }).click()
    await page.getByText('Document added').waitFor({ timeout: 30000 })
    await page.locator('.document-row').filter({ hasText: 'E2E pasted note' }).waitFor()
    await dialog.waitFor({ state: 'hidden' })
    const dismiss = page.getByRole('button', { name: 'Dismiss notification' })
    if (await dismiss.count()) {
      await dismiss.first().click()
    }
  })

  await check('deleting a document requires confirmation and removes it', async () => {
    await page.getByRole('button', { name: 'Delete E2E pasted note' }).click()
    const confirm = page.getByRole('dialog')
    await confirm.getByRole('heading', { name: 'Delete this document?' }).waitFor()
    await confirm.getByRole('button', { name: 'Delete document' }).click()
    await page.locator('.document-row').filter({ hasText: 'E2E pasted note' }).waitFor({
      state: 'detached',
      timeout: 15000,
    })
  })

  await check('ask returns a cited answer and retrieved chunks', async () => {
    await page.getByRole('link', { name: /Ask/ }).click()
    await page.getByRole('heading', { name: 'Ask your knowledge base', level: 1 }).waitFor()
    await page.getByRole('textbox', { name: 'Your question' }).fill('How does HNSW search work?')
    await page.getByRole('button', { name: 'Ask question' }).click()
    const heading = page.locator('#answer-heading')
    await heading.waitFor({ timeout: 45000 })
    assert(/^Answer with citations$/.test(await heading.innerText()), 'expected a cited answer')
    const sources = page.locator('.source-list')
    await sources.waitFor()
    const sourceCount = await page.locator('.source').count()
    assert(sourceCount >= 1, `expected at least one source, got ${sourceCount}`)
    assert((await page.locator('.source-list').innerText()).length > 40, 'source text looks empty')
    assert((await page.locator('.answer-text').innerText()).length > 20, 'answer text looks empty')
    assert((await page.locator('.citation').count()) > 0, 'answer should render at least one citation')
    await page.locator('.rail-status').filter({ hasText: 'Index online' }).waitFor({ timeout: 15000 })
    const shellCount = await page.locator('.topbar__documents').innerText()
    assert(!shellCount.includes('Loading'), `shell stayed in a loading state: "${shellCount}"`)
    assert(/\d+\s+documents?/.test(shellCount), `shell shows no document count: "${shellCount}"`)
    await page.screenshot({ path: path.join(SHOTS, 'ask-answer.png') })
  })

  await check('map projects the corpus and exposes an inspectable list', async () => {
    await page.getByRole('link', { name: /Map/ }).click()
    await page.getByRole('heading', { name: 'The shape of your knowledge', level: 1 }).waitFor()
    await page.getByRole('heading', { name: '2D projection' }).waitFor({ timeout: 30000 })
    const svg = page.locator('svg[role="group"]')
    await svg.waitFor()
    const points = svg.locator('[role="button"]')
    assert((await points.count()) > 0, 'expected projected points')
    const overlay = page.getByRole('button', { name: 'Query overlay' })
    assert((await overlay.getAttribute('aria-pressed')) === 'true', 'query overlay should be on')
    await overlay.click()
    assert((await overlay.getAttribute('aria-pressed')) === 'false', 'overlay did not toggle off')
    await overlay.click()
    await page.getByLabel('Inspect a chunk').selectOption({ index: 1 })
    await page.waitForTimeout(150)
    assert((await page.locator('.point-detail').innerText()).length > 20, 'inspector stayed empty')
    await page.screenshot({ path: path.join(SHOTS, 'map-2d.png') })
    await page.getByRole('button', { name: '3D', exact: true }).click()
    await page.locator('canvas').waitFor({ timeout: 15000 })
    await page.getByRole('button', { name: '2D', exact: true }).click()
    await page.getByRole('heading', { name: '2D projection' }).waitFor()
  })

  await check('benchmark page renders measured results and switches efSearch', async () => {
    await page.getByRole('link', { name: /Benchmark/ }).click()
    await page.getByRole('heading', { name: 'Measured, not estimated', level: 1 }).waitFor()
    await page.getByRole('heading', { name: 'Recall vs. efSearch' }).waitFor({ timeout: 30000 })
    const picker = page.getByRole('group', { name: 'Select efSearch' })
    const ef100 = picker.getByRole('button', { name: '100', exact: true })
    await ef100.click()
    assert((await ef100.getAttribute('aria-pressed')) === 'true', 'efSearch 100 not selected')
    assert((await page.locator('.report-hero h2').innerText()).includes('recall@10'), 'no recall headline')
    assert((await page.locator('.report-hero h2').innerText()).includes('%'), 'recall headline lacks a value')
    const rows = await page.locator('table tbody tr').count()
    assert(rows >= 2, `expected a measurement table, got ${rows} rows`)
    await page.screenshot({ path: path.join(SHOTS, 'benchmark.png') })
  })

  await check('theme toggle changes and persists the theme', async () => {
    const html = page.locator('html')
    const before = await html.getAttribute('data-theme')
    const target = before === 'dark' ? 'light' : 'dark'
    await page.getByRole('button', { name: `Switch to ${target} mode` }).first().click()
    await page.waitForFunction(
      (expected) => document.documentElement.dataset.theme === expected,
      target,
    )
    await page.reload({ waitUntil: 'domcontentloaded' })
    await page.getByRole('heading', { name: 'Measured, not estimated', level: 1 }).waitFor()
    assert(
      (await html.getAttribute('data-theme')) === target,
      `theme ${target} did not survive a reload`,
    )
    await page.getByRole('button', { name: `Switch to ${before} mode` }).first().click()
    await page.waitForFunction(
      (expected) => document.documentElement.dataset.theme === expected,
      before,
    )
  })

  await check('mobile layout has no horizontal overflow and exposes navigation', async () => {
    await page.setViewportSize({ width: 390, height: 844 })
    await page.goto(`${BASE}/#/documents`, { waitUntil: 'domcontentloaded' })
    await page.getByRole('heading', { name: 'Documents', level: 1 }).waitFor()
    await page
      .locator('.metric')
      .filter({ has: page.locator('.metric__label', { hasText: 'Dimensions' }) })
      .locator('.metric__value')
      .filter({ hasText: '256' })
      .waitFor({ timeout: 15000 })
    const overflow = await page.evaluate(
      () => document.documentElement.scrollWidth - window.innerWidth,
    )
    assert(overflow <= 1, `mobile page overflows horizontally by ${overflow}px`)
    await page.screenshot({ path: path.join(SHOTS, 'mobile-documents.png') })
    await page.getByRole('button', { name: 'Open navigation' }).click()
    const rail = page.locator('#primary-sidebar')
    await page.waitForTimeout(400)
    const opened = await rail.evaluate((element) => ({
      inert: element.inert,
      x: element.getBoundingClientRect().x,
    }))
    assert(!opened.inert, 'navigation drawer is still inert after opening')
    assert(opened.x >= -1, `navigation drawer stayed off-screen at x=${opened.x}`)
    await rail.getByRole('button', { name: 'Close navigation' }).click()
    await page.waitForTimeout(400)
    const closed = await rail.evaluate((element) => ({
      inert: element.inert,
      right: element.getBoundingClientRect().right,
    }))
    assert(closed.inert, 'navigation drawer is still interactive after closing')
    assert(closed.right <= 1, `navigation drawer stayed on-screen at right=${closed.right}`)
  })

  await check('skip link targets the main landmark', async () => {
    await page.setViewportSize({ width: 1440, height: 1000 })
    // A real navigation resets Chromium's sequential focus starting point.
    // Without it, Tab resumes after whichever control was focused before.
    await page.goto(`${BASE}/?e2e=skip#/documents`, { waitUntil: 'domcontentloaded' })
    await page.getByRole('heading', { name: 'Documents', level: 1 }).waitFor()
    assert((await page.locator('main#main-content').count()) === 1, 'missing main landmark')
    const skip = page.locator('.skip-link')
    assert((await skip.getAttribute('href')) === '#main-content', 'skip link target is wrong')
    await page.keyboard.press('Tab')
    assert(await skip.evaluate((node) => node === document.activeElement), 'skip link is not first in tab order')
    await page.waitForTimeout(250)
    const box = await skip.boundingBox()
    assert(box !== null && box.y >= 0, 'focused skip link stayed off-screen')
  })

  await check('keyboard dialogs trap focus and restore it on close', async () => {
    await page.getByRole('button', { name: 'Add documents' }).click()
    const dialog = page.getByRole('dialog')
    await dialog.waitFor()
    const title = dialog.getByLabel(/Title/)
    await title.waitFor()
    assert(await title.evaluate((node) => node === document.activeElement), 'title field not focused')
    await page.keyboard.press('Escape')
    await dialog.waitFor({ state: 'hidden' })
    const trigger = page.getByRole('button', { name: 'Add documents' })
    assert(
      await trigger.evaluate((node) => node === document.activeElement),
      'focus was not restored to the trigger',
    )
  })

  await check('no console errors, page errors, failed requests, or error responses', async () => {
    assert(consoleErrors.length === 0, `console errors:\n${consoleErrors.join('\n')}`)
    assert(pageErrors.length === 0, `page errors:\n${pageErrors.join('\n')}`)
    assert(failedRequests.length === 0, `failed requests:\n${failedRequests.join('\n')}`)
    assert(badResponses.length === 0, `4xx/5xx responses:\n${badResponses.join('\n')}`)
  })

  await check('screenshots were written', async () => {
    const shots = fs.readdirSync(SHOTS).filter((name) => name.endsWith('.png'))
    assert(shots.length >= 4, `expected screenshots, found ${shots.length}`)
  })

  await context.close()
  await browser.close()

  console.log(`\n${passed} passed, ${failed} failed`)
  if (failures.length) {
    console.log('\nFailures:')
    for (const failure of failures) {
      console.log(`- ${failure.name}: ${failure.message}`)
    }
  }
  process.exit(failed === 0 ? 0 : 1)
}

main().catch((error) => {
  console.error(error)
  process.exit(1)
})
