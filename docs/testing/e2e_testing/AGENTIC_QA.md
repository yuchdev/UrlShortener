# Agentic QA Specification

## QA architecture

The QA system is split into:

- section specs in `docs/qa/sections/*.md`
- executable section scripts in `tests/e2e/scripts/sections/*.sh`
- expected DB snapshots in `tests/e2e/expected/*.json`
- orchestration scripts in `tests/e2e/scripts/run_section.sh` and `tests/e2e/scripts/run_all_sections.sh`
- assertion engine in `tools/sqlite_state_assert.py`

Each section is isolated and runs its own full lifecycle.

## Execution model

- Run one section: `bash tests/e2e/scripts/run_section.sh 03_redirects`
- Run all sections: `bash tests/e2e/scripts/run_all_sections.sh`
- Partial run: `QA_SECTIONS="01_server_boot,03_redirects" bash tests/e2e/scripts/run_all_sections.sh`

## Test lifecycle

Each section follows mandatory phases:

1. reset environment (`--reset-database`)
2. start services
3. runtime readiness assertion (port/process/http)
4. execute scenario actions
5. verify database snapshot (`--expect`)
6. verify runtime assertions again
7. cleanup (stop processes + reset DB)

## Snapshot and assertion philosophy

- DB correctness is validated by deterministic JSON snapshots (`mode=exact` or `mode=contains`).
- Runtime correctness is validated with helper runtime assertions:
  - port open/closed
  - HTTP status/body/redirect target
  - process running
  - log content
- Assertions are explicit in every section and are CLI-only.

## Reset philosophy

`--reset-database` keeps credential/auth identity tables (for example `app_users`) and removes transient tables (links, analytics, fingerprints, sessions, temporary runtime data).

## Failure interpretation

Any failed assertion is terminal for that section. The helper emits failure artifacts under timestamped folders in `qa_failures/` (or configured failure dir), including summary JSON and available DB/log/stdout/stderr diagnostics.

## Retry behavior

Runtime assertions retry with exponential backoff for up to 30 seconds by default:

- `--retry-timeout-seconds` (default `30`)
- `--retry-initial-delay-seconds` (default `0.25`)

## Expected logging

Sections must write service logs to `tests/e2e/failures/<section>/` (or another explicit path), and use `--assert-log-contains` for key milestones like service startup and critical actions.

## Agent execution guidance

- Run sections independently while iterating on one area.
- Keep runs idempotent by always resetting DB before and after a section.
- Do not assume shared state between sections.
- On failure, inspect `summary.json` first, then DB snapshot and logs.
