# Hierarchical Navigable Small World graphs

HNSW is a graph-based index for approximate nearest neighbour search. It organises vectors into a stack of proximity graphs. The upper layers are sparse and act as an express lane, while the bottom layer contains every vector and supports fine-grained search. The structure is inspired by skip lists: a long-range hop at the top quickly reaches the right region, then lower layers refine the result.

## Construction

A new vector receives a random level drawn from a geometric distribution. The vector is inserted into every layer up to that level. At each layer the algorithm runs a small beam search to find nearby neighbours, then connects the vector to the best `M` candidates. The bottom layer uses a larger degree, usually `M0 = 2 * M`. Candidate selection is diversified: a candidate is kept only when it is closer to the new vector than to an already selected neighbour. This pruning avoids a cluster of redundant edges and keeps the graph navigable.

HNSWhy uses `M = 16`, `M0 = 32`, and `efConstruction = 200` by default. Construction is deterministic for a fixed seed, which makes benchmark runs repeatable. The graph stores integer references into an in-memory vector store, so it never duplicates the embedding payload.

## Search

A query starts at the entry point in the highest layer. At each upper layer the algorithm greedily moves to a neighbour that is closer to the query, stopping when no neighbour improves the distance. It then descends to the next layer with the current best candidate. At layer zero the greedy walk becomes a beam search: a candidate list of size `efSearch` is maintained, and the search stops when the nearest unexplored candidate is farther than the worst result in the list.

`efSearch` is the main latency-recall knob. A larger beam visits more vectors and usually improves recall, but each extra hop costs distance computations. The benchmark shows the trade-off clearly: on SIFT-100k, `efSearch = 10` gives about 0.87 Recall@1 at roughly 21k queries per second, while `efSearch = 50` gives about 0.99 Recall@1 at roughly 6.9k queries per second.

## Deletion and maintenance

Deleting a vector directly from a proximity graph can disconnect paths, so HNSWhy uses tombstones. The slot stays in the graph and remains traversable, but it is filtered from query results and excluded from live counts. A rebuild from SQLite is the recovery path when the graph becomes inconsistent. SQLite remains the source of truth: the graph is an in-memory acceleration structure that is reconstructed at startup.

## Trade-offs

HNSW offers excellent query latency at high recall, but it uses more memory than a flat index and its build time grows with `efConstruction`. The graph is also approximate: results are not guaranteed to be exact nearest neighbours. For applications that need an exact baseline, HNSWhy also includes a brute-force index and uses it to measure recall and ground truth.
