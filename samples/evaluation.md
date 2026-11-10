# Evaluating retrieval quality

HNSWhy measures the search engine and the answer layer separately. Engine quality is recall against exact nearest neighbours. Answer quality depends on the retrieved context, the prompt, and the chat provider. Mixing the two hides whether a weak answer came from bad retrieval or from a weak generator.

## Ground truth

For a benchmark dataset, every query vector is compared against every base vector with squared L2 distance. The exact top results become ground truth. An approximate search then runs with a chosen `efSearch`, and recall is the fraction of true neighbours that appear in its result list. Recall@1 asks whether the single nearest vector was found. Recall@10 asks how many of the true top ten appear in the returned ten.

This definition is strict and reproducible. It does not require labels, human judgements, or an LLM. The same query set and the same ground truth are used for brute force and HNSW, so the comparison is direct.

## Latency and throughput

Queries per second is the number of measured queries divided by total query time. Latency percentiles describe the distribution rather than a single average: p50 is the median, p95 captures the slow tail, and p99 shows the worst common case. The benchmark runs a warmup pass so page-cache and branch-predictor effects do not dominate the first query.

Build time and peak resident memory are reported alongside query metrics. A fast index that takes minutes to build or consumes several gigabytes may still be the wrong choice for a small corpus. The report includes the HNSW parameters, compiler, CPU, operating system, build type, and AVX2 status so results can be compared honestly.

## Reading the curve

On SIFT-100k with 128-dimensional vectors, HNSW at `efSearch = 50` reached roughly 0.993 Recall@1 and 0.984 Recall@10 at about 6,895 queries per second. At `efSearch = 10` it reached about 0.875 Recall@1 at roughly 21,000 queries per second. At `efSearch = 200` recall rose to about 0.9998 while throughput fell to roughly 2,283 queries per second. Exact brute force reached perfect recall at about 193 queries per second.

The useful output is the whole trade-off curve, not one number. A question answering product usually prefers high recall at interactive latency. A visual map or autocomplete feature can tolerate lower recall for faster responses. The benchmark page shows the measured points and never estimates missing ones.

## RAG evaluation

For the answer layer, the relevant questions are whether the retrieved chunks actually support the answer, whether every claim is cited, and whether the system refuses when the library has no relevant content. The extractive provider makes these properties visible without a network model: it selects sentences from the retrieved sources, attaches numbered citations, and falls back to a refusal when overlap is too low. A hosted model uses a grounded prompt with the same citation contract.
