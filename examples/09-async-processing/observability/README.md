# Chapter 9 observability: where the off-path telemetry lands

This directory is the optional "where it lands" layer for "Asynchronous
processing": a destination for the off-path metrics
`ch09::TelemetrySink` can emit, never a requirement for the chapter's
measured numbers. `examples/09-async-processing/results/` is produced
entirely host-side; this stack exists only to show the off-path export
actually arriving somewhere real.

## Bring it up

```bash
docker compose -f examples/09-async-processing/observability/docker-compose.yml up -d
```

This starts one container, `docker.io/grafana/otel-lgtm:0.9.3` (the house
`lgtm-docker-stack` all-in-one image: Grafana, Loki, Tempo, Mimir, and an
OpenTelemetry Collector), exposing:

- `http://localhost:4318`: the OTLP/HTTP receiver `ch09::TelemetrySink`
  posts to
- `http://localhost:3000`: the Grafana UI, anonymous admin access (dev
  only; never do this outside a throwaway local stack)

## Point the consumer at it

```bash
export CH09_OTLP_ENDPOINT=127.0.0.1:4318
examples/09-async-processing/demo.sh run      # C++ lane
examples/09-async-processing/demo.sh rust     # Rust lane
```

`CH09_OTLP_ENDPOINT` unset (the default) disables export entirely: the
consumer's `record_batch_flush()` becomes a no-op, and `demo.sh` never
depends on this stack. Set it only to confirm the export path lands
somewhere real.

## What to look for in Grafana

Open `http://localhost:3000`, add an Explore query against the
auto-provisioned Prometheus datasource, and query (confirmed live against
this compose file: `docker exec ch09-lgtm curl -s
'http://localhost:9090/api/v1/label/__name__/values'` lists both once the
consumer has flushed at least one batch with `CH09_OTLP_ENDPOINT` set):

- `ch09_batch_size_ratio`: the record count flushed in each batch (should
  sit near the chapter's `batch_capacity` under steady load). The `_ratio`
  suffix comes from this metric's OTLP unit (`"1"`, a plain count), which
  Prometheus's naming convention renders that way; the value itself is a
  count, not a ratio.
- `ch09_flush_duration_us_microseconds`: each batch flush's own duration
  in microseconds, the same quantity `flush_blocking_p50_us` /
  `flush_io_uring_p50_us` in `results/async-processing.txt` summarize
  across a whole run.

Both arrive labelled `service.name="ch09-async-processing"` (see
`ch09::build_otlp_json` in `cpp/src/telemetry_sink.cpp`).

## Tear down

```bash
docker compose -f examples/09-async-processing/observability/docker-compose.yml down
```

## Why a hand-rolled OTLP/HTTP POST instead of opentelemetry-cpp

The background consumer already runs on its own thread, off the hot path;
the lesson here is placement (telemetry never touches the hot path), not
which OTel SDK a C++ service should reach for in production. Pulling in
the full `opentelemetry-cpp` SDK (and its own dependency tree) for a demo
whose point is "off the hot path, optional, best-effort" would add weight
without adding to that point. A real service should reach for
`opentelemetry-cpp`'s OTLP HTTP exporter instead of hand-building the JSON
body the way `ch09::TelemetrySink` does; see "Trade-offs" in the chapter.
