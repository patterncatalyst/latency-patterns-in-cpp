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
