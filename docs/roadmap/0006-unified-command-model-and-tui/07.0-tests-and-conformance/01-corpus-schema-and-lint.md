# 01 - Corpus schema and lint

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** Task 01.0 (envelope shape, error codes)
**Blocks:** 02, 03, 05, 11, 12, 13, 14
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Fix the exact on-disk format of a conformance corpus case and ship a stdlib-only
Python linter that rejects malformed corpora, so every later subtask (shims,
corpus authoring, coverage gate) reads one unambiguous schema.

## Dependencies

- [Command catalog](/docs/commands/README.md) sections 1 (envelope, error table) and 7.
- Task 01.0 for the final error-code list (the lint reads codes from the catalog table, not a hard-coded copy).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/commands/README.md` | create | Authoritative schema + authoring guide (content below) |
| `tests/conformance/commands/00_schema_selftest.json` | create | Minimal valid corpus used by the lint self-test (2 cases) |
| `tools/conformance_lint.py` | create | Validates every `tests/conformance/commands/*.json` |
| `tests/conformance/py/00_corpus_lint.py` | create | unittest wrapper: runs the linter on the real corpus + negative fixtures |
| `tests/conformance/py/lint_negative/*.json` | create | One broken file per lint rule (about 12 files) |
| `sources.cmake`, `CMakeLists.txt` | modify | Introduce `CONFORMANCE_PY_SOURCES` + loop (README conventions); add `00_corpus_lint.py` |

## Classes and functions

**Corpus file (schema v1).** UTF-8 JSON object:

```json
{
  "corpus_version": 1,
  "suite": "link_lifecycle",
  "description": "create/get/update/delete round trips",
  "allow_unknown_codes": false,
  "cases": [ { "...": "Case" } ]
}
```

`allow_unknown_codes` is optional (default `false`); only the self-test corpus sets it to `true`.

**Case** (all keys lower_snake_case; unknown keys are lint errors):

| Key | Type | Req | Meaning |
|---|---|---|---|
| `name` | string `^[a-z][a-z0-9_]*$`, globally unique | yes | Prefix by group, e.g. `link_create_conflict` |
| `doc` | string | yes | One-line scenario description |
| `command` | string | yes | Primary command under test (feeds the coverage matrix) |
| `caller` | `"admin"` \| `"user"` | no, default `admin` | Default role for all steps (`user` = `ControlSet::User`) |
| `backend` | `"sqlite"` \| `"inmemory"` | no, default `sqlite` | `inmemory` is legal only if `transports.only` excludes `cli_cmd` and `rest_*` out-of-process runners |
| `files` | object name -> text | no | Materialized in the per-case temp dir; referenced as `${tmp}/<name>` |
| `transports` | `{ "only": [ids] }` or `{ "skip": { id: "reason" } }` | no | ids: `direct`,`rest_generic`,`rest_resource`,`cli_cmd`,`stdio`,`tui`; every skip needs a non-empty reason |
| `steps` | array of Step, length >= 1 | yes | Executed in order on one fresh store; **all** steps are asserted |

**Step:**

| Key | Type | Req | Meaning |
|---|---|---|---|
| `label` | string | no | Shown in failure output |
| `caller` | role | no | Overrides the case role for this step |
| `request` | Request envelope object | one of | Exactly the catalog section 1 request (`v`, optional `id`, `command`, `args`, `options`) |
| `raw_request` | string | one of | Verbatim text sent instead of an envelope (invalid JSON, missing `command`, bad `v`). Carried only by transports with capability `carries_raw` |
| `expect` | Expect | yes | See below |
| `save` | object var -> JSON path | no | Captures from the actual response, e.g. `{"slug": "$.result.slug"}`; usable as `${slug}` in later steps |
| `await_job` | `{ "var": "job_id", "timeout_ms": 5000 }` | no | After this step, poll `job.get {id: ${job_id}}` until `state` is terminal; the **final** `job.get` response is what `expect` checks (the accepting response is checked via `expect_accept`) |
| `expect_accept` | Expect | only with `await_job` | Checks the immediate `job` envelope (`{"ok":true,"job":{"id":"<id>","state":"queued"}}`) |
| `rest_resource` | `{ "method", "path", "query"?, "body"?, "status" }` | no | Equivalent resource-route call for adapter `rest_resource`; absent = step runs through `rest_generic` on that adapter |
| `http_status` | int | no | Override the status derived from the catalog error table for `rest_generic` |
| `cli_exit` | int | no | Override exit code derived from the catalog table for `cli_cmd` |

**Expect:**

```json
{ "ok": true,  "result": { "slug": "docs", "created_at": "<ts>", "id": "<id>" }, "match": "exact" }
{ "ok": false, "error":  { "code": "slug_conflict" } }
{ "ok": true,  "job":    { "id": "<id>", "state": "queued" } }
```

- Exactly one of `result` / `job` / `error` (matching `ok`).
- `error.message` is **never** specified or compared (catalog: message is for humans).
- `match`: `"exact"` (default; extra keys in actual fail) or `"subset"` (extra object keys in actual allowed; arrays still exact).
- Envelope `v` is always compared (`1`); envelope `id` is compared only against the request `id` on transports with capability `carries_id`; when the request has no `id` the response `id` must match `^srv-[0-9]+$`.
- Matcher tokens and `${var}` substitution: defined in subtask 02.

**Lint rules (each has a negative fixture):**

1. Valid JSON, `corpus_version == 1`, no unknown keys at any level.
2. Case names unique across **all** corpus files; matches name regex.
3. Exactly one of `request` / `raw_request` per step.
4. `expect` shape matches `ok`; `error.code` is in the catalog error table (parsed from `docs/commands/README.md` section 1) or the file is marked `"allow_unknown_codes": true` (only the self-test uses it).
5. `request.command` is a dotted `<group>.<verb>` string unless the step uses `raw_request` or the case name starts with `unknown_command_`.
6. Literal timestamps are forbidden in `expect` under keys `created_at`, `updated_at`, `deleted_at`, `started_at`, `finished_at` (must be `<ts>`); literal values under any key named `id` must be a matcher token or `${var}`.
7. `${var}` used before the step that `save`s it is an error; `${tmp}` always allowed.
8. `await_job` requires `expect_accept`; `expect_accept` forbidden without `await_job`.
9. `transports.skip` reasons non-empty; ids known; `only` and `skip` mutually exclusive.
10. `backend: "inmemory"` rule from the Case table.
11. `files` keys contain no path separators.
12. Role/permission sanity: a step whose `command` is mutating per the catalog and whose effective caller is `user` must expect `error.code == "not_authorized"`.

`tools/conformance_lint.py` signature: `main(argv) -> int` (exit 0 clean, 1 on any finding, prints `file:case:step: RULE-n message`), plus importable `lint_paths(paths: list[Path]) -> list[Finding]` used by the wrapper test.

## Implementation steps

1. Write `tests/conformance/commands/README.md` with the schema above verbatim plus 3 annotated examples (success, error, job).
2. Implement `lint_paths` (pure functions per rule, one `Finding(rule, file, case, step, msg)` dataclass).
3. Parse the catalog error table with a regex over the `### Error codes` table rows (first backticked cell(s) per row; handle `` `invalid_slug` / `reserved_slug` ``).
4. Create negative fixtures; `00_corpus_lint.py` asserts each fixture yields exactly its rule id and the real corpus yields none.
5. Register the wrapper in `CONFORMANCE_PY_SOURCES`.

## Commands

```bash
python tools/conformance_lint.py tests/conformance/commands/*.json
ctest --test-dir cmake-build -R "^conformance_00_corpus_lint$" --output-on-failure
```

(Windows: `-C Debug`; the shell glob does not expand in PowerShell - pass the directory, the tool globs itself: `python tools/conformance_lint.py tests/conformance/commands`.)

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/py/00_corpus_lint.py` | `conformance_00_corpus_lint` | `integration;conformance` | real corpus clean; each of the 12 negative fixtures yields its rule; self-test corpus clean |

## Constraints

- Python stdlib only (no `jsonschema` dependency; CI installs none).
- Linter must not import or run the binary.
- Schema changes after merge are additive only; bump `corpus_version` if not.

## Success criteria

1. `README.md` documents every key above and no key exists in the linter that is undocumented.
2. All 12 rules have a failing fixture and the linter returns exit 1 on each.
3. `conformance_00_corpus_lint` passes on Windows and Linux.
4. Lint wall time on 500 cases is under 2 s.

## Out of scope / risks

- JSON Schema (draft-07) export: not needed; revisit if an external consumer appears.
- Error-code list changes in 01.0 are picked up automatically via the catalog parse; if the catalog table format changes the parser breaks loudly (rule 4 test).
