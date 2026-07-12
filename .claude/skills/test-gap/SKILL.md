---
name: test-gap
description: User-invoked as /test-gap [path]. Spawns the testing-expert agent to analyse test coverage and returns a prioritised list of missing tests, risk-ranked (redirect/lifecycle/SSRF logic first). Use to decide what to test next.
allowed-tools: Read, Grep, Glob, Bash, Agent
invocation: /test-gap [path]
---

# Test Gap Analysis

Identify the highest-value missing tests for `$ARGUMENTS` (a module/dir, or the
whole project if empty).

## Steps

1. Establish current coverage using the project's `coverage` CMake target
   (defined in `cmake/coverage.cmake`, backed by lcov/genhtml):
   ```bash
   cmake --build <build> --target coverage
   ```
   This produces `coverage_html/` and an lcov summary. When a path is given, scope
   your reading of the report to that module's files. If a coverage-instrumented
   build is not available, fall back to reading the CTest suite
   (`ctest --test-dir <build> -N`) and mapping tests to source by inspection, and
   say so in the output.
2. Spawn the **`testing-expert`** agent with the coverage output (or the CTest
   inventory) and the target path. Ask it to map uncovered lines to behaviours and
   **risk-rank** them using the full
   [references/risk-ranking-rubric.md](references/risk-ranking-rubric.md)
   (blast-radius criteria + URL Shortener module examples per tier):
   - **P0**: redirect resolution, link-lifecycle precedence, cache-aside
     invalidation, SSRF/`isPrivateHost` checks, auth on management routes,
     parameterized-SQL boundaries.
   - **P1**: storage backend strategies (sqlite/postgres/redis), config parsing,
     analytics recording (best-effort, must not break redirect).
   - **P2**: CLI/formatting, logging, metrics labels.
3. Return the testing-expert agent's prioritised list.

## Output

```
## Test Gap - <target> (coverage: NN%)
### P0 - must test
- path:line-range - <untested behaviour> - <why risky> - <suggested test>
### P1 - should test
### P2 - nice to have
```
Offer to have `testing-expert` write the P0 tests next.

## Completion checklist

- [ ] Coverage established via the `coverage` target (or CTest inventory fallback, stated explicitly), scoped to the target when given
- [ ] Every uncovered branch mapped to a *behaviour*, not just a line number
- [ ] Findings tiered P0/P1/P2 by blast radius per [references/risk-ranking-rubric.md](references/risk-ranking-rubric.md) - not by coverage-% delta
- [ ] Redirect / lifecycle / cache-invalidation / SSRF / auth / SQL gaps ranked P0
- [ ] Each P0 has a concrete suggested test (input → expected outcome)
- [ ] Prioritised list returned; offer made to have `testing-expert` write the P0 tests
