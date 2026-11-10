export interface StoreStats {
  documents: number
  chunks: number
  embeddings: number
}

export interface ProviderHealth {
  provider: string
  model: string
  available: boolean
}

export interface HealthResponse {
  status: 'ok' | 'degraded' | 'error'
  database: string
  store: StoreStats
  embedding: ProviderHealth
  chat: ProviderHealth
}

export interface IndexStats {
  live: number
  slots: number
  tombstones: number
  dim: number
}

export interface StatusResponse {
  database: string
  store: StoreStats
  index: IndexStats
  embedding: {
    provider: string
    model: string
    dim: number
  }
  chat: {
    provider: string
    model: string
  }
  query_ef: number
  sample_loaded: boolean
  benchmark_available: boolean
}

export interface DocumentSummary {
  id: number
  title: string
  source: string
  chunk_count: number
  char_count: number
  created_at: string
}

export interface ChunkRecord {
  id: number
  ordinal: number
  text: string
}

export interface DocumentDetail {
  document: DocumentSummary
  text: string
  chunks: ChunkRecord[]
}

export interface AskSource {
  chunk_id: number
  document_id: number
  title: string
  ordinal: number
  text: string
  distance: number
}

export interface AskTimings {
  embedding: number
  search: number
  generate: number
  total: number
}

export interface AskModels {
  embedding_provider: string
  embedding_model: string
  chat_provider: string
  chat_model: string
}

export interface AskResponse {
  answer: string
  sources: AskSource[]
  query_vector: number[]
  timings_ms: AskTimings
  models: AskModels
}

export interface MapPoint {
  chunk_id: number
  document_id: number
  title: string
  ordinal: number
  preview: string
  vector: number[]
}

export interface MapResponse {
  total_chunks: number
  sampled: number
  points: MapPoint[]
}

export interface LatencyStats {
  mean: number
  p50: number
  p95: number
  p99: number
  min?: number
  max?: number
}

export interface BenchMethod {
  name: 'hnsw' | 'bruteforce'
  ef_search?: number
  recall_at_1: number
  recall_at_10: number
  qps: number
  latency_ms: LatencyStats
  avg_distance_computations: number
  queries_measured: number
}

export interface BenchReport {
  schema_version: number
  generated_at_utc: string
  engine: {
    name: string
    version: string
  }
  dataset: {
    name: string
    base_count: number
    query_count: number
    dim: number
    ground_truth_k: number
    base_file: string
    queries_file: string
    ground_truth_file: string
    base_bytes?: number
    base_file_bytes?: number
    queries_bytes?: number
  }
  provenance?: {
    run_id: string
    git_commit: string
    worktree_dirty: boolean
    base_prefix: number
    base_sha256: string
    queries_sha256: string
    ground_truth_ids_sha256: string
    ground_truth_distances_sha256: string
    cxx_flags_release: string
  }
  config: {
    M: number
    M0: number
    ef_construction: number
    seed: number
    k: number
    passes: number
    warmup_queries: number
    build_type: string
    distance: string
  }
  build: {
    build_time_ms: number
    peak_rss_mb: number
    rss_after_build_mb: number
  }
  environment: {
    compiler: string
    cpu: string
    hardware_threads: number
    os: string
    avx2: boolean
    build_type: string
    hostname: string
  }
  methods: BenchMethod[]
}

export type BenchmarkResponse =
  | { available: true; report: BenchReport }
  | { available: false; message: string; command: string }
