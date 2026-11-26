# Diagrams

Each diagram is a paired `name.svg` (committed, embedded via the `excalidraw.html`
include) + `name.excalidraw` (editable source). Generate both with
`scripts/generate_diagram.py` (see `references/diagram-engine.md`). Catalogue them
here as you add them:

| Diagram | Chapter | What it shows |
|---|---|---|
| `scan-to-divert-line` | 0 | The parcel sortation line's five stages, with the scan-to-divert measurement window bracketed under scan and sort |
| `mean-versus-tail` | 1 | A schematic scan-to-divert distribution: a fast-path cluster and a separate stall tail, with naive mean, naive p99, and corrected p99 marked to show where each one actually lands |
| `budget-decomposition` | 2 | The five stages in process order with each one's measured p50 contribution, bracketing the three (scan, label, sort) that sum into the scan-to-divert window measured against the physical deadline |
| `eliminating-work-before-after` | 3 | The scan decode path's before and after lanes stacked: the before lane's allocate, hash-map lookup, and table-rebuild steps, each marked as eliminated, are simply absent from the after lane's single precomputed array index, with each lane's measured p50 |
| `zero-copy-layout-and-false-sharing` | 4 | Four panels: the recent-scan ledger as a ring of pointers to scattered heap records versus one contiguous hot-field array with cold fields set apart, and two per-thread counters sharing one 64-byte cache line versus split onto their own |
| `wait-free-spsc-handoff` | 5 | Two panels: the mutex-guarded queue, where push takes a lock and pop blocks in a condition_variable, versus the wait-free SPSC ring, where the producer's release store into `slot[tail]` and the consumer's acquire load of `slot[head]` hand off without either thread ever blocking |
| `exploiting-concurrency-fanout` | 6 | Two panels: data-parallel, where a batch is split into fixed slices up front and one `jthread` worker classifies each slice with no shared mutable state; task-parallel, where every parcel becomes one task pulled dynamically off a shared queue by thread-pool workers |
| `caching-cache-and-sieve` | 7 | Two panels: a cache-aside lookup, where a hit returns straight from the cache and a miss resolves through the slow `RouteResolver`, inserts, and returns; SIEVE's hand scanning a queue from the back toward the front, clearing a visited bit as a second chance and evicting the first entry still unvisited |
| `shard-per-core-and-skew` | 8 | Two panels: four cores all serializing through one mutex into one shared map, versus four shards each owning its own map with no cross-shard lock, one shard's map drawn larger and amber to show a skewed key distribution overloading it while the rest idle |
| `async-processing-offpath-handoff` | 9 | Two panels: the hot path writing its audit record synchronously, one durable flush per parcel, versus the hot path enqueuing into a bounded ring and returning immediately, drained by a background event-loop consumer that batches the flush and ships telemetry off-path to an optional LGTM stack |
