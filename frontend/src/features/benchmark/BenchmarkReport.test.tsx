import { render, screen, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { describe, expect, it, vi } from 'vitest'
import type { BenchReport } from '../../api/types'
import { BenchmarkReport } from './BenchmarkReport'

const report: BenchReport = {
  schema_version: 1,
  generated_at_utc: '2026-11-10T14:04:58Z',
  engine: { name: 'HNSWhy', version: '0.1.0' },
  dataset: {
    name: 'SIFT-100k',
    base_count: 100_000,
    query_count: 10_000,
    dim: 128,
    ground_truth_k: 100,
    base_file: 'sift_base.fvecs',
    queries_file: 'sift_query.fvecs',
    ground_truth_file: 'gt.ivecs',
  },
  config: {
    M: 16,
    M0: 32,
    ef_construction: 200,
    seed: 42,
    k: 10,
    passes: 3,
    warmup_queries: 200,
    build_type: 'release',
    distance: 'squared_l2',
  },
  build: { build_time_ms: 36_125, peak_rss_mb: 92.5, rss_after_build_mb: 88 },
  environment: {
    compiler: 'GCC 16.2.1',
    cpu: 'Intel Core i5-12450H',
    hardware_threads: 12,
    os: 'Linux',
    avx2: false,
    build_type: 'release',
    hostname: 'bench-host',
  },
  methods: [
    {
      name: 'hnsw',
      ef_search: 10,
      recall_at_1: 0.87,
      recall_at_10: 0.8,
      qps: 2000,
      latency_ms: { mean: 0.5, p50: 0.46, p95: 0.65, p99: 0.76 },
      avg_distance_computations: 120,
      queries_measured: 30_000,
    },
    {
      name: 'hnsw',
      ef_search: 50,
      recall_at_1: 0.99,
      recall_at_10: 0.98,
      qps: 1000,
      latency_ms: { mean: 1, p50: 0.9, p95: 1.1, p99: 1.2 },
      avg_distance_computations: 400,
      queries_measured: 30_000,
    },
    {
      name: 'bruteforce',
      recall_at_1: 1,
      recall_at_10: 1,
      qps: 100,
      latency_ms: { mean: 10, p50: 9, p95: 11, p99: 12 },
      avg_distance_computations: 100_000,
      queries_measured: 10_000,
    },
  ],
}

describe('BenchmarkReport', () => {
  it('renders the stored report and recomputes ratios when efSearch changes', async () => {
    const user = userEvent.setup()
    render(<BenchmarkReport report={report} onRefresh={vi.fn()} />)

    const hero = screen.getByRole('region', { name: 'Headline results' })
    expect(within(hero).getByText('98.00%')).toBeInTheDocument()
    expect(
      screen.getByText(
        '10.0× the throughput of the exact scan while retaining 98.0% of its top-10 results.',
      ),
    ).toBeInTheDocument()
    expect(screen.getByText('3 rows')).toBeInTheDocument()

    const picker = screen.getByRole('group', { name: 'Select efSearch' })
    await user.click(within(picker).getByRole('button', { name: '10' }))

    expect(within(hero).getByText('80.00%')).toBeInTheDocument()
    expect(
      screen.getByText(
        '20.0× the throughput of the exact scan while retaining 80.0% of its top-10 results.',
      ),
    ).toBeInTheDocument()
    expect(within(picker).getByRole('button', { name: '10' })).toHaveAttribute(
      'aria-pressed',
      'true',
    )
  })

  it('shows the honest empty state when the report has no HNSW rows', () => {
    const bruteOnly = { ...report, methods: [report.methods[2]] }
    render(<BenchmarkReport report={bruteOnly} onRefresh={vi.fn()} />)

    expect(screen.getByText('No HNSW measurements in this report.')).toBeInTheDocument()
    expect(screen.getByText('1 row')).toBeInTheDocument()
  })
})
