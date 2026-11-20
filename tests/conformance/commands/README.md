# Command conformance corpus - schema (v1) and authoring guide

This directory holds the **command conformance corpus**: a set of UTF-8 JSON
files, one *suite* per file, each a list of *cases* replayed verbatim through
every transport (direct registry, REST generic, REST resource routes, `cmd`
CLI, `serve-stdio`, TUI `InProcessClient`). The corpus is **data, not code** -
adding a transport adds one shim and reuses every case here unchanged.

This file is the authoritative schema. Its executable mirror is
[`tools/conformance_lint.py`](/tools/conformance_lint.py), run in CI by
`conformance_00_corpus_lint`. Every key documented here is enforced by the
linter and every key the linter knows is documented here; the two must not
drift. Schema changes after merge are **additive only** - otherwise bump
`corpus_version`.

Error codes and the mutating-command set are **not** copied into the linter;
they are parsed live from the command catalog
([`docs/commands/README.md`](/docs/commands/README.md) section 1 error table
and section 3.7 generated table), so a code added by Task 01.0 is picked up
automatically.

## Corpus file

A corpus file is a UTF-8 JSON object:

```json
{
  "corpus_version": 1,
  "suite": "link_lifecycle",
  "description": "create/get/update/delete round trips",
  "allow_unknown_codes": false,
  "cases": [ { "...": "Case" } ]
}
```

| Key | Type | Req | Meaning |
|---|---|---|---|
| `corpus_version` | integer | yes | Schema version. Only `1` is defined. |
| `suite` | string | yes | Short suite id (also the logical grouping in reports). |
| `description` | string | no | One-line human description of the suite. |
| `allow_unknown_codes` | bool | no, default `false` | Relax rule 4's "code in catalog" check. **Only** the self-test corpus sets it to `true`. |
| `cases` | array of Case | yes | The cases of this suite. |

## Case

All keys are lower_snake_case; unknown keys are lint errors.

| Key | Type | Req | Meaning |
|---|---|---|---|
| `name` | string `^[a-z][a-z0-9_]*$`, globally unique | yes | Prefix by group, e.g. `link_create_conflict`. |
| `doc` | string | yes | One-line scenario description. |
| `command` | string | yes | Primary command under test (feeds the coverage matrix). |
| `caller` | `"admin"` \| `"user"` | no, default `admin` | Default role for all steps (`user` = `ControlSet::User`). |
| `backend` | `"sqlite"` \| `"inmemory"` | no, default `sqlite` | `inmemory` is legal only if `transports.only` is present and excludes every out-of-process runner (see rule 10). |
| `files` | object name -> text | no | Materialized in the per-case temp dir; referenced as `${tmp}/<name>`. Keys must be safe bare names (non-empty, no path separator, not `.`/`..`, not starting with `~`). |
| `transports` | `{ "only": [ids] }` or `{ "skip": { id: "reason" } }` | no | ids: `direct`, `rest_generic`, `rest_resource`, `cli_cmd`, `stdio`, `tui`; every skip needs a non-empty reason; `only` and `skip` are mutually exclusive. |
| `steps` | array of Step, length >= 1 | yes | Executed in order on one fresh store; **all** steps are asserted. |

## Step

| Key | Type | Req | Meaning |
|---|---|---|---|
| `label` | string | no | Shown in failure output. |
| `caller` | role | no | Overrides the case role for this step. |
| `request` | Request envelope object | one of | Exactly the catalog section 1 request (`v`, optional `id`, `command`, `args`, `options`). |
| `raw_request` | string | one of | Verbatim text sent instead of an envelope (invalid JSON, missing `command`, bad `v`). Carried only by transports with capability `carries_raw`. |
| `expect` | Expect | yes | See below. |
| `save` | object var -> JSON path | no | Captures from the actual response, e.g. `{"slug": "$.result.slug"}`; usable as `${slug}` in later steps. |
| `await_job` | `{ "var": "job_id", "timeout_ms": 5000 }` | no | After this step, poll `job.get {id: ${job_id}}` until `state` is terminal; the **final** `job.get` response is what `expect` checks. `var` names a variable saved by this or an earlier step. |
| `expect_accept` | Expect | only with `await_job` | Checks the immediate `job` envelope (`{"ok":true,"job":{"id":"<id>","state":"queued"}}`). |
| `rest_resource` | `{ "method", "path", "query"?, "body"?, "status" }` | no | Equivalent resource-route call for adapter `rest_resource`; absent = step runs through `rest_generic` on that adapter. |
| `http_status` | int | no | Override the status derived from the catalog error table for `rest_generic`. |
| `cli_exit` | int | no | Override exit code derived from the catalog table for `cli_cmd`. |

Exactly one of `request` / `raw_request` is required per step.

## Expect

```json
{ "ok": true,  "result": { "slug": "docs", "created_at": "<ts>", "id": "<id>" }, "match": "exact" }
{ "ok": false, "error":  { "code": "slug_conflict" } }
{ "ok": true,  "job":    { "id": "<id>", "state": "queued" } }
```

| Key | Type | Req | Meaning |
|---|---|---|---|
| `ok` | bool | yes | Whether the envelope is a success. |
| `result` | object | one of | Expected `result` match object (success). |
| `job` | object | one of | Expected `job` match object (accepted). |
| `error` | `{ "code": string }` | one of | Expected error; **only** `code` is allowed. |
| `match` | `"exact"` \| `"subset"` | no, default `exact` | `exact`: extra keys in actual fail. `subset`: extra object keys in actual allowed; arrays still exact. |

- Exactly one of `result` / `job` / `error`, matching `ok` (`error` iff
  `ok: false`).
- `error.message` is **never** specified or compared (catalog: the message is
  for humans).
- Envelope `v` is always compared (`1`); envelope `id` is compared only against
  the request `id` on transports with capability `carries_id`; when the request
  has no `id` the response `id` must match `^srv-[0-9]+$`.
- Literal timestamps are forbidden under `created_at` / `updated_at` /
  `deleted_at` / `started_at` / `finished_at`: use the `<ts>` matcher. Any value
  under a key named `id` must be a matcher token (`<id>`) or a `${var}`.
- Matcher tokens and `${var}` substitution are defined fully in the
  "Matchers and normalization" section below.

## Matchers and normalization

The corpus is replayed by two independent implementations - a Python harness and
a C++ runner - so the rules for turning a live response envelope into a
pass/fail verdict are defined **once, as data**, in
[`../normalizer_vectors.json`](/tests/conformance/normalizer_vectors.json). That
file is the single normative source: both implementations must pass every vector
in it unchanged, and any rule change edits the vectors first. Its executable
Python mirror is
[`../py/conformance_match.py`](/tests/conformance/py/conformance_match.py), run
in CI by `conformance_01_matcher_vectors`. The prose below documents the rules
the vector file pins down.

### Normalization (applied to the *actual* envelope before matching)

| Rule | Effect |
|---|---|
| N1 | `error.message` and any `error.details` are deleted (the message is for humans and is never compared). |
| N2 | Object key order is ignored; objects compare structurally. |
| N3 | Numbers compare by value (`1` equals `1.0`); a boolean is never equal to a number. |
| N4 | Envelope `id`: if the request carried an `id` and the transport has capability `carries_id`, the actual `id` must equal it; otherwise the actual `id` must match `^srv-[0-9]+$`. After the check the value is replaced by the sentinel `"<id>"`, so `expect` need not restate it. |
| N5 | A response carrying any top-level key other than `v`, `id`, `ok`, and one of `result` / `job` / `error` fails, regardless of `match` mode. |
| N6 | Strings compare after Unicode NFC normalization; there is no trimming. |

N1, N4 and N5 are performed by `normalize_actual`; N2, N3 and N6 are realized by
the matcher itself.

### Matcher tokens

A token is a whole-string value in `expect`; tokens are the only wildcard
mechanism and never match object keys, only values. A `<...>` string that is not
one of the tokens below is exact text (so `<foo>` matches only the literal
`"<foo>"`).

| Token | Matches |
|---|---|
| `<any>` | any JSON value, including `null` |
| `<id>` | a non-empty string, <= 64 chars, drawn from `[A-Za-z0-9_.:-]` (link ids, job ids `j-<n>`, server ids `srv-<n>`) |
| `<ts>` | an RFC 3339 UTC timestamp `YYYY-MM-DDThh:mm:ss[.fff]Z` (ASCII, UTC only) |
| `<int>` / `<uint>` | an integer / an integer >= 0 (an integral JSON number such as `3.0` counts; a boolean does not) |
| `<slug:N>` | a string of exactly `N` characters from the slug alphabet `[0-9A-Za-z]` (generated slugs; `N` is the configured generated length, default `SHORTENER_GENERATED_SLUG_LENGTH` = 7) |
| `<regex:PATTERN>` | a full-string match of `PATTERN`, restricted to constructs valid in both Python `re` and C++ `std::regex` ECMAScript (no lookbehind, no named groups, no possessive quantifiers) and free of catastrophic-backtracking shapes (see below) |
| `<var:name>` | the value previously saved as `name` (compared with the N3 / N6 value rules) |
| `<absent>` | used only as an object field value; asserts the key is **not** present |

A `<regex:PATTERN>` is additionally rejected **before it is ever executed** when
`PATTERN` risks catastrophic backtracking / ReDoS: the conservative syntactic
rule flags a quantified group whose body itself contains a quantifier or an
alternation (`(a+)+`, `(a*)*`, `(.*a){20}`, `(a|a)+`). Such a pattern produces a
mismatch finding rather than running. Both the Python and the C++ matcher apply
this rule identically - `std::regex` is more prone to blowing up on these - so
the check lives in the shared vector file and must not diverge.

### Variable substitution

`${name}` substitution is applied to every string inside a request and an
`expect` before use. `${tmp}` expands to the per-case temp directory with `/`
separators on every OS. A reference to an unsaved variable is an error.

### Match modes

`exact` (default) rejects any extra object key in the actual value; `subset`
allows extra **object** keys but still compares arrays element-for-element and by
length. In both modes `<absent>` asserts a key is missing, and an N5 violation
fails the response regardless of mode.

## Lint rules

Each rule has a negative fixture under
[`../py/lint_negative/`](/tests/conformance/py/lint_negative) named
`rule_NN_<desc>.json`, verified by `conformance_00_corpus_lint`.

1. **Structure.** Valid JSON; `corpus_version == 1`; required keys present; no
   unknown keys at any structured level (corpus, case, step, request envelope,
   expect, transports, await_job, rest_resource); `caller` (where present) is
   `admin` or `user`.
2. **Case name.** Matches `^[a-z][a-z0-9_]*$` and is unique across **all**
   corpus files.
3. **Request shape.** Exactly one of `request` / `raw_request` per step.
4. **Expect shape.** `ok` is boolean; exactly one of `result` / `job` / `error`
   consistent with `ok`; `match` (if present) is `exact` or `subset`; `error`
   carries only `code`; `code` is in the catalog error table, unless the file
   is marked `allow_unknown_codes: true` (self-test only). Applies to both
   `expect` and `expect_accept`.
5. **Command name.** `request.command` is a dotted `<group>.<verb>`, unless the
   step uses `raw_request` or the case name starts with `unknown_command_`.
6. **No literals for volatile fields.** In `expect` / `expect_accept`, timestamp
   keys must be the `<ts>` matcher; values under a key named `id` must be a
   matcher token or `${var}`.
7. **Variable order.** A `${var}` must be saved by this or an earlier step;
   `${tmp}` is always allowed.
8. **Job coupling.** `await_job` requires `expect_accept`; `expect_accept` is
   forbidden without `await_job`.
9. **Transports.** `skip` reasons are non-empty; ids are known; `only` and
   `skip` are mutually exclusive.
10. **Backend.** `backend` is `sqlite` or `inmemory`; `inmemory` requires a
    `transports.only` list that excludes every out-of-process runner
    (`cli_cmd`, `stdio`, `rest_generic`, `rest_resource`) - only the in-process
    `direct` and `tui` transports can share an in-memory store.
11. **File names.** `files` keys are safe bare file names: non-empty, no path
    separator (`/` or `\`), not `.` or `..`, and not starting with `~`.
12. **Role sanity.** A step whose `command` is mutating (per the catalog) and
    whose effective caller is `user` must expect `error.code ==
    "not_authorized"`.
13. **Unicode NFC.** Every string in `request` / `raw_request` / `expect` /
    `expect_accept` (values and object keys) must already be in Unicode NFC
    (`unicodedata.normalize("NFC", s) == s`). The C++ runner's N6 normalization
    composes only the Latin-1 accented range (it carries no full Unicode data
    table, forbidden by CLAUDE.md), so a corpus that relied on composing a wider
    code point would make the C++ and Python matchers disagree. Requiring
    pre-normalized corpus strings keeps the two verdicts identical; author
    accented text in its composed form.
14. **Confined `path` arguments.** Any `path` argument in a step's
    `request.args` must be exactly `${tmp}/<name>`, where `<name>` is a single
    safe bare name (the rule 11 vocabulary: non-empty, no path separator, not
    `.`/`..`, not starting with `~`). A literal absolute path, a `..` traversal,
    or more than one component is rejected. Separately, any case that uses a
    `path` arg in **any** step must scope its transports (`transports.only` or
    `transports.skip`), so a "path works" expectation is never measured against a
    transport that refuses `path` by default (REST is default-deny `unsupported`,
    `command_runtime.cpp`; only `cmd`, `tui` and `serve-stdio` grant the file-path
    capability).

    **Why the corpus is the confinement, not the shim.** A `path` arg is free
    text; unlike a `files` key, neither the C++ case runner nor the Python
    harness inspects or rewrites it. The in-process `direct`/`tui` shims pin a
    confinement `root` to the per-case temp dir, but `cmd` ships no `--path-root`
    flag, so there is no per-shim way to confine the `cmd` replay. This lint
    rule - requiring every `path` to be `${tmp}/<name>` - is therefore the single
    control that keeps every transport's replay inside the temp dir and off the
    real filesystem.

## Running the linter

```bash
python tools/conformance_lint.py tests/conformance/commands/*.json
ctest --test-dir cmake-build -R "^conformance_00_corpus_lint$" --output-on-failure
```

On Windows / PowerShell the shell glob does not expand; pass the directory and
the tool globs it:

```powershell
python tools/conformance_lint.py tests/conformance/commands
```

## Annotated examples

### Success - create a link

```json
{
  "name": "link_create_ok",
  "doc": "admin creates a link and gets a result envelope",
  "command": "link.create",
  "steps": [
    {
      "label": "create",
      "request": {
        "v": 1,
        "command": "link.create",
        "args": { "target_url": "https://example.com/docs", "slug": "docs" }
      },
      "expect": {
        "ok": true,
        "result": { "slug": "docs", "created_at": "<ts>", "id": "<id>" },
        "match": "subset"
      },
      "save": { "slug": "$.result.slug" }
    }
  ]
}
```

### Error - duplicate slug

```json
{
  "name": "link_create_conflict",
  "doc": "creating a duplicate slug returns slug_conflict",
  "command": "link.create",
  "steps": [
    {
      "request": { "v": 1, "command": "link.create",
                   "args": { "target_url": "https://example.com/a", "slug": "dup" } },
      "expect": { "ok": true, "result": { "slug": "dup" }, "match": "subset" }
    },
    {
      "request": { "v": 1, "command": "link.create",
                   "args": { "target_url": "https://example.com/b", "slug": "dup" } },
      "expect": { "ok": false, "error": { "code": "slug_conflict" } }
    }
  ]
}
```

### Job - async import with await

```json
{
  "name": "link_import_job",
  "doc": "import runs as a job; await the terminal job.get",
  "command": "link.import",
  "files": { "pairs.csv": "full_url,short_url\nhttps://example.com/a,a\n" },
  "steps": [
    {
      "label": "import",
      "request": {
        "v": 1,
        "command": "link.import",
        "args": { "format": "csv", "path": "${tmp}/pairs.csv" },
        "options": { "async": true }
      },
      "save": { "job_id": "$.job.id" },
      "expect_accept": { "ok": true, "job": { "id": "<id>", "state": "queued" } },
      "await_job": { "var": "job_id", "timeout_ms": 5000 },
      "expect": { "ok": true, "result": { "imported": 1 }, "match": "subset" }
    }
  ]
}
```
