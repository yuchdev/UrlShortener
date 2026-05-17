# Benchmarking Notes (Stage 05)

## Environment template

Document for every benchmark run:
- CPU model / vCPU count
- memory
- storage type
- kernel and distro
- compiler + flags
- backend topology

## Tooling examples

```bash
wrk -t4 -c128 -d30s http://127.0.0.1:8080/r/abc123
hey -n 100000 -c 128 http://127.0.0.1:8080/healthz
python3 scripts/benchmark_redirect.py --url http://127.0.0.1:8080/r/abc123 --seconds 60
```

## Baseline report format

| Route / scenario | RPS | p50 ms | p95 ms | p99 ms | error % | CPU % | RSS MB |
|---|---:|---:|---:|---:|---:|---:|---:|
| redirect cache hit |  |  |  |  |  |  |  |
| redirect cache miss + repository hit |  |  |  |  |  |  |  |
| create link |  |  |  |  |  |  |  |
| stats endpoint |  |  |  |  |  |  |  |

## Required comparisons

- analytics enabled vs disabled (when Stage 04 analytics is compiled/enabled)
- before/after instrumentation changes
- steady-state + saturation profile

## Methodology

- Warm up before measurement window.
- Use identical datasets/config between runs.
- Report median of >=3 runs and include variance.
- Keep load tests non-blocking for CI pass/fail unless explicitly gated.
