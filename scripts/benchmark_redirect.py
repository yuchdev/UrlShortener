#!/usr/bin/env python3
"""Stage 1 redirect micro-benchmark harness.

This script measures hot-path and mixed-outcome redirect latency and throughput
using concurrent HTTP GET traffic.
"""

import argparse
import concurrent.futures
import time
import urllib.request
import urllib.error


def percentile(values, pct):
    """Return the nearest-rank percentile for numeric samples."""
    if not values:
        return 0.0
    sorted_values = sorted(values)
    idx = min(len(sorted_values) - 1, int(round((pct / 100.0) * (len(sorted_values) - 1))))
    return sorted_values[idx]


def worker(stop_at, url, expected):
    """Issue repeated GET requests until stop time and collect matching-status latencies."""
    latencies = []
    total = 0
    while time.time() < stop_at:
        start = time.perf_counter()
        code = 0
        try:
            req = urllib.request.Request(url, method="GET")
            opener = urllib.request.build_opener(urllib.request.HTTPRedirectHandler())
            with opener.open(req, timeout=2.0) as resp:
                code = resp.status
        except urllib.error.HTTPError as err:
            code = err.code
        except Exception:
            code = -1
        latency_ms = (time.perf_counter() - start) * 1000.0
        if code == expected:
            latencies.append(latency_ms)
        total += 1
    return total, latencies


def run_case(base, path, expected, concurrency, duration):
    """Run a single benchmark case and compute throughput/latency summary."""
    stop_at = time.time() + duration
    url = f"{base.rstrip('/')}/{path.lstrip('/')}"
    totals = []
    latencies = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
        futures = [executor.submit(worker, stop_at, url, expected) for _ in range(concurrency)]
        for future in concurrent.futures.as_completed(futures):
            total, lats = future.result()
            totals.append(total)
            latencies.extend(lats)

    req_total = sum(totals)
    rps = req_total / duration if duration > 0 else 0.0
    return {
        "requests": req_total,
        "rps": rps,
        "p50": percentile(latencies, 50),
        "p95": percentile(latencies, 95),
        "p99": percentile(latencies, 99),
    }


def main():
    """Parse arguments, run benchmark cases, and print summary lines."""
    parser = argparse.ArgumentParser(description="Simple Stage-1 redirect microbenchmark")
    parser.add_argument("--base-url", default="http://127.0.0.1:28080")
    parser.add_argument("--hot-path", default="benchhot")
    parser.add_argument("--mixed-paths", default="benchhot,missing01,exp001,off001")
    parser.add_argument("--concurrency", type=int, default=32)
    parser.add_argument("--duration", type=int, default=10)
    args = parser.parse_args()

    print(f"base_url={args.base_url} concurrency={args.concurrency} duration={args.duration}s")

    hot = run_case(args.base_url, args.hot_path, 302, args.concurrency, args.duration)
    print("[hot-path]")
    print(f"requests={hot['requests']} rps={hot['rps']:.2f} p50_ms={hot['p50']:.3f} p95_ms={hot['p95']:.3f} p99_ms={hot['p99']:.3f}")

    mixed_paths = [p.strip() for p in args.mixed_paths.split(",") if p.strip()]
    results = []
    for path in mixed_paths:
        expected = 302 if path == args.hot_path else 404 if path.startswith("missing") else 410
        results.append((path, run_case(args.base_url, path, expected, args.concurrency, args.duration)))

    print("[mixed]")
    for path, result in results:
        print(
            f"path={path} requests={result['requests']} rps={result['rps']:.2f} "
            f"p50_ms={result['p50']:.3f} p95_ms={result['p95']:.3f} p99_ms={result['p99']:.3f}"
        )


if __name__ == "__main__":
    main()
