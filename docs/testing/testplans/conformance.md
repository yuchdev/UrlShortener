# Conformance Test Plan

## Scope

The command conformance corpus proves that the shared command registry produces
identical `result` structure and `error.code` across every transport.
One corpus file = one suite; each suite is replayed verbatim through all six
transport shims. The corpus is **data, not code**: adding a transport adds one
shim and reuses every case unchanged.

This plan covers:

- the corpus format and authoring rules;
- the six transport adapters and their skip/capability semantics;
- the lint, coverage, catalog-drift, and authz-matrix gates;
- the commands used to run, lint, and report conformance;
- the acceptance criteria;
- the relationship to the e2e analogs.

## Corpus format

The corpus lives in `tests/conformance/commands/`. Each `*.json` file is a suite
of cases following the schema documented in
`tests/conformance/commands/README.md`.

The linter (`tools/conformance_lint.py`) enforces rules 1-14; the negative
fixtures under `tests/conformance/py/lint_negative/` cover every rule. Schema
changes after merge are **additive only** (bump `corpus_version` for
incompatible changes).

### Suites

| File | Suite | Commands |
|---|---|---|
| `10_link_create.json` | `link_create` | `link.create` |
| `11_link_read.json` | `link_read` | `link.get`, `link.preview`, `link.stats` |
| `12_link_update.json` | `link_update` | `link.update` |
| `13_link_state.json` | `link_state` | `link.delete`, `link.enable`, `link.disable`, `link.restore` |
| `20_link_list.json` | `link_list` | `link.list` |
| `21_db_tables.json` | `db_tables` | `db.tables`, `db.rows` |
| `22_system.json` | `system` | `system.info`, `command.list` |
| `30_link_import.json` | `link_import` | `link.import` |
| `31_link_export.json` | `link_export` | `link.export` |
| `32_job_commands.json` | `job_commands` | `job.get`, `job.list`, `job.cancel` |
| `40_envelope_errors.json` | `envelope_errors` | invalid envelopes |
| `41_authorization.json` | `authorization` | read-only denials |
| `42_authorization_matrix.json` | `authorization_matrix` | authz matrix cases |

## Transport adapters

| Transport id | How run | In-process | `carries_id` | `carries_raw` | Default skip policy |
|---|---|---|---|---|---|
| `direct` | C++ `CaseRunner` via `CommandRegistry::execute()` directly | yes | yes | no | none |
| `tui` | C++ `CaseRunner` via `InProcessClient` (TUI client seam) | yes | yes | no | none |
| `cli_cmd` | Python shim spawns `url_shortener cmd` | no | yes (echoes via `--id`) | no | none |
| `stdio` | Python shim spawns `url_shortener serve-stdio` via stdin | no | yes | yes | none |
| `rest_generic` | Python shim calls `POST /api/v1/commands` | no | yes | yes | skipped when `path` args present (default-deny) |
| `rest_resource` | Python shim calls the mapped REST route | no | partially (resource routes) | no | skipped when no REST analog |

A corpus case may declare `transports.skip` with a reason or `transports.only`
to restrict which transports run it. `inmemory` backend cases must use
`transports.only` and exclude every out-of-process runner.

The Python harness is run with `python3 -I` (isolated mode) to prevent picking
up installed packages. Temp directories are named per-process (`${tmp}` expands
to a per-case unique path) to prevent collisions under `ctest -j`.

## Matcher vectors

The normalization rules (N1-N6) and matcher tokens (`<any>`, `<id>`, `<ts>`,
`<int>`, `<uint>`, `<slug:N>`, `<regex:PATTERN>`, `<var:name>`, `<absent>`)
are defined as executable vectors in
`tests/conformance/normalizer_vectors.json`. Both the Python harness
(`tests/conformance/py/conformance_match.py`) and the C++ support library
(`tests/conformance/support/`) must pass every vector unchanged.

The vector file is the single normative source; a rule change edits the vectors
first.

## Gates

| Gate | Test | What it checks |
|---|---|---|
| Corpus lint | `conformance_00_corpus_lint` | All rules 1-14 on every `commands/*.json` file. |
| Matcher vectors | `conformance_01_matcher_vectors` | Python harness passes every `normalizer_vectors.json` case. |
| Harness self-test | `conformance_02_harness_selftest` | Python harness infrastructure (temp dirs, variable substitution, `await_job`, summary writing). |
| Coverage gate | `conformance_20_coverage_gate` | Every catalog command and error code is exercised on every transport (or waived); reads `$URLSHORTENER_CONFORMANCE_OUT` summaries; waivers in `tools/conformance_waivers.json`. |
| Authz-matrix drift | `conformance_authz_matrix_check` | Authz-matrix corpus (`42_authorization_matrix.json`) matches the descriptor matrix computed from the live registry. |

Three error codes are waived in `tools/conformance_waivers.json`:
`storage_failure` (unit-proven), `internal` (unit-proven), and `busy`
(fixture-proven in `conformance__01_direct_registry_corpus`). Each waiver
carries an owner, a reason, and a repo-relative evidence file; the gate
verifies the evidence file exists.

## Commands

```bash
# Lint the corpus (no build required):
python3 tools/conformance_lint.py tests/conformance/commands/*.json

# Run all conformance tests:
ctest --test-dir cmake-build -L conformance --output-on-failure

# List conformance tests without running:
ctest --test-dir cmake-build -N -L conformance

# Run one runner:
ctest --test-dir cmake-build -R "^conformance__01_direct_registry_corpus$" --output-on-failure

# Run the Python harness transport runners:
ctest --test-dir cmake-build -R "^conformance_1[0-4]_" --output-on-failure

# Coverage report (after a conformance run with URLSHORTENER_CONFORMANCE_OUT set):
python3 tools/conformance_coverage.py \
    --catalog docs/commands/README.md \
    --waivers tools/conformance_waivers.json \
    --report "$URLSHORTENER_CONFORMANCE_OUT"
```

On Windows add `-C Debug` to every `cmake --build` and `ctest` invocation.

## Acceptance criteria

1. Every corpus case produces the same `ok` value, `result` structure (subset
   match), and `error.code` on every transport that the case does not explicitly
   skip.
2. `error.message` is never compared (N1 normalization removes it before
   matching); only `error.code` is asserted.
3. The lint gate (`conformance_00_corpus_lint`) is clean on every commit that
   touches `tests/conformance/commands/*.json`.
4. The coverage gate (`conformance_20_coverage_gate`) passes; a new catalog
   command or error code without corpus coverage is a gate failure.
5. The authz-matrix drift check (`conformance_authz_matrix_check`) is green.

## Adding a corpus case

1. Choose the matching suite file (or create a new one with a unique `suite`
   id if no suitable file exists).
2. Write the case following the schema in
   `tests/conformance/commands/README.md`. Use the `seed7` standard seed from
   that document when the case needs a populated store.
3. Run `python3 tools/conformance_lint.py tests/conformance/commands/*.json`
   and fix any findings.
4. Run `ctest --test-dir cmake-build -L conformance` to prove all applicable
   transports pass.
5. If the case exercises a new command or error code, run
   `ctest --test-dir cmake-build -R "^conformance_20_coverage_gate$"` to
   confirm the coverage gate still passes.

## E2E analogs

The conformance suites run in-process or against the built binary (not a live
server). The `serve-stdio` and `cmd` process-level scenarios that relate to the
no-listener guarantee are covered by:

- `e2e_19_cmd_serve_stdio_no_listener` — proves `cmd`, `serve-stdio`, and
  non-TTY `tui` never bind a listening socket and never write `uri.txt`.
- `e2e_30_serve_stdio` — full NDJSON session through the live binary.

See [`e2e.md`](./e2e.md) for the full e2e plan.
