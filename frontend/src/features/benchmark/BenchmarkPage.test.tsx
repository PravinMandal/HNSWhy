import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { BenchmarkPage } from './BenchmarkPage'

const apiMocks = vi.hoisted(() => ({
  benchmark: vi.fn(),
}))

vi.mock('../../api/client', () => ({ api: apiMocks }))

const unavailable = {
  available: false,
  message: 'No benchmark report has been generated yet.',
  command: 'scripts/run_bench.sh',
}

describe('BenchmarkPage', () => {
  beforeEach(() => {
    apiMocks.benchmark.mockReset()
  })

  it('shows the command and can check again when no report exists', async () => {
    const user = userEvent.setup()
    apiMocks.benchmark.mockResolvedValue(unavailable)
    render(<BenchmarkPage />)

    expect(
      await screen.findByRole('heading', { name: 'No benchmark report found' }),
    ).toBeInTheDocument()
    expect(screen.getByText('No benchmark report has been generated yet.')).toBeInTheDocument()
    expect(screen.getByText('scripts/run_bench.sh')).toBeInTheDocument()
    expect(apiMocks.benchmark).toHaveBeenCalledTimes(1)

    await user.click(screen.getByRole('button', { name: 'Check again' }))
    await waitFor(() => expect(apiMocks.benchmark).toHaveBeenCalledTimes(2))
  })

  it('shows an error notice and retries a failed load', async () => {
    const user = userEvent.setup()
    apiMocks.benchmark.mockRejectedValueOnce(new Error('the benchmark endpoint is offline'))
    apiMocks.benchmark.mockResolvedValueOnce(unavailable)
    render(<BenchmarkPage />)

    expect(await screen.findByRole('alert')).toHaveTextContent(
      'the benchmark endpoint is offline',
    )
    await user.click(screen.getByRole('button', { name: 'Try again' }))
    expect(
      await screen.findByRole('heading', { name: 'No benchmark report found' }),
    ).toBeInTheDocument()
  })

  it('shows a loading status until the request settles', () => {
    apiMocks.benchmark.mockReturnValue(new Promise(() => {}))
    render(<BenchmarkPage />)

    expect(
      screen.getByRole('status', { name: 'Loading the benchmark report' }),
    ).toBeInTheDocument()
  })
})
