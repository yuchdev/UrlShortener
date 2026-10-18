# 15 - Corpus coverage gate

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 11, 12, 13, 14, 06, 07, 08, 09, 10 (the shims feed the transport matrix report)
**Blocks:** 34
**Estimated size:** S
**Suggested agent:** testing-expert

## Objective

A CTest-registered tool that fails when the corpus does not cover the live
registry: every command, every catalog error code, every REST route in the
section 5 table, and every transport has at least one executed case; and that
the generated authorization matrix is current.

## Dependencies

- Subtasks 11-14 for content; `command.list` (01.0/10) from the built binary, whose registry comes from `build_command_runtime` (03.0/18, 05.0/20), as the registry's source of truth; catalog error table.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tools/conformance_coverage.py` | create | The gate |
| `tools/conformance_waivers.json` | create | Explicit, reviewed waivers (starts with `storage_failure`, `internal`) |
| `tests/conformance/py/20_coverage_gate.py` | create | Wrapper test; runs the tool |
| `sources.cmake` | modify | Append wrapper |

## Classes and functions

```python
def registry_commands(binary: str) -> list[dict]          # runs: <bin> cmd command.list  (envelope -> result.commands)
def catalog_error_codes(catalog: Path) -> set[str]         # shared parser with tools/conformance_lint.py
def corpus_index(corpus_dir: Path) -> CorpusIndex          # cases by command, by error code, by rest_resource route
def check(index, commands, codes, waivers) -> list[Finding]
def main(argv) -> int          # --binary PATH, --catalog PATH, --corpus DIR, --report FILE(optional markdown matrix)
```

Checks (each finding names the missing item):

1. **Command coverage:** for each registry command, at least one case where it is the `command` of a step whose `expect.ok == true` (or, for commands that only produce jobs, `expect_accept`), and at least one failing case unless the descriptor has no documented error (read-only `command.list`, `system.info`).
2. **Error-code coverage:** each code from the catalog table appears as `expect.error.code` somewhere in the corpus or is waived with a justification and the name of the unit test that covers it (`tools/conformance_waivers.json`: `{"storage_failure": "tests/unit/app/18_link_command_service_update_storage_failure.cpp", "internal": "<01.0 unit test>"}`; the tool verifies the named file exists).
3. **Route coverage:** each row of catalog section 5 has a step with `rest_resource` using that method+path pattern.
4. **Mutating coverage:** each `mutating` command has an `authz_matrix_*` case (`tools/conformance_gen_authz.py --check` clean).
5. **Dry-run coverage:** each descriptor with `dry_run: true` has a case with `options.dry_run`; each without has an `unsupported` case (at least one representative, not per-command).
6. **Orphans:** corpus cases whose `command` is not in the registry and not `unknown_command_*` fail (stale after a rename).
7. **Matrix report:** with `--report`, emit a markdown table commands x transports (ran/skipped) from the last run's JSON summaries (`$TMP/conformance_summary_<transport>.json`, written by every runner; see Implementation steps) for the PR description.

## Implementation steps

1. Make every runner (C++ and Python) write a summary JSON `{transport, ran, skipped:[{case,reason}]}` to `$URLSHORTENER_CONFORMANCE_OUT` (default: temp dir) - add this to subtask 03/05 code when implementing (tiny addition; note in their PRs).
2. Implement the tool; invoke the binary via `subprocess` with `SIMPLE_HTTP_BIN`.
3. Wrapper test calls `main()` and asserts exit 0; also negative unit tests with synthetic corpus and fake registry JSON (the tool takes `--registry-json FILE` to bypass the binary).
4. Document waiver policy: each waiver needs an owner file; new waivers require reviewer approval (comment in the JSON header).

## Commands

```bash
python tools/conformance_coverage.py --binary cmake-build/url_shortener --catalog docs/commands/README.md --corpus tests/conformance/commands --report coverage_matrix.md
ctest --test-dir cmake-build -R "^conformance_20_coverage_gate$" --output-on-failure
```

(Windows: `--binary cmake-build\Debug\url_shortener.exe`.)

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/py/20_coverage_gate.py` | `conformance_20_coverage_gate` | `integration;conformance` | real corpus passes; synthetic: missing command, missing error code, missing route, stale waiver file, orphan case, stale authz matrix |

## Constraints

- Must run after the full conformance suite so summaries exist; CTest `DEPENDS` set for `conformance_20_coverage_gate` on the runner tests (use `set_tests_properties(... DEPENDS ...)` only among tests that exist in the configuration).
- No waivers for any command; waivers apply to error codes only.

## Success criteria

1. Gate fails (exit 1, readable message) when a command is added to the registry without a corpus case: demonstrated by the synthetic test.
2. Gate passes on the real corpus with at most the two documented waivers.
3. Matrix report shows 6 transports x all commands with no unexplained skips.

## Out of scope / risks

- Does not measure code coverage (that is subtask 34's `/test-gap`).
- Risk: reading `command.list` from the binary couples the gate to 04.0 `cmd`; fallback `--registry-json` is provided for in-process use.
