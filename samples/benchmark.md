# Benchmark methodology

The HNSWhy benchmark is deliberately independent of the HTTP server and the language model. It loads a fixed dataset, builds the index, runs a query workload, and writes a JSON report. Keeping the benchmark offline makes results comparable across machines and prevents network latency from contaminating engine measurements.

## Dataset and ground truth

The headline run uses SIFT-100k: 100,000 base vectors with 128 dimensions and 10,000 query vectors. Ground truth is computed by exact squared-L2 search over the full base set, keeping the 100 nearest neighbours for each query. Recall is measured against that exact result. The benchmark records the dataset name, counts, dimensions, file paths, and a checksum-friendly schema version in the report.

## Metrics

Recall@1 and Recall@10 measure how often the true nearest neighbour, or the true top ten, appear in the approximate result. QPS is the number of measured queries divided by elapsed query time. Latency is reported as mean, p50, p95, and p99 over all measured queries. Build time and peak resident memory are recorded separately. The report also stores the HNSW configuration, compiler, CPU, operating system, build type, and whether AVX2 was enabled.

Each query is run multiple times with a warmup pass, and the median or aggregated measurements are written to the report. The benchmark does not hide slow runs: the exact brute-force baseline is included in the same report, so the speedup and the recall cost are visible together.

## What the numbers mean

On the recorded SIFT-100k run, HNSW with `efSearch = 50` reached about 0.993 Recall@1 and 0.984 Recall@10 at roughly 6,895 queries per second. The exact brute-force baseline reached perfect recall at roughly 193 queries per second. Increasing `efSearch` to 200 pushed Recall@1 to about 0.9998 while reducing throughput to roughly 2,283 queries per second. These are measured results from one machine, not universal guarantees.

The important comparison is the shape of the curve, not a single headline number. A user can choose a point on the latency-recall curve that fits the application. For a question answering system, high recall at moderate latency is usually more valuable than the absolute fastest query. For an interactive map or autocomplete feature, a lower `efSearch` may be appropriate.

## Reproducing the run

The benchmark requires the SIFT dataset and a release build. The helper scripts download and unpack the data, build the binaries, compute ground truth, and run the benchmark. The resulting report is written to `docs/results/bench.json` and served read-only by the application. The server never runs the benchmark during a request; it only displays the last recorded report.
