# 02 - Matcher and normalizer vectors

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** 03, 05
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Define, once and as data, how an actual response envelope is normalized and
matched against an `expect` block (volatile ids, timestamps, generated slugs),
ship the Python implementation, and publish a vector file that the C++
implementation (subtask 03) must also pass - so the two matchers cannot drift.

## Dependencies

- Subtask 01 (schema: matcher tokens appear in `expect`).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/normalizer_vectors.json` | create | About 60 vectors (format below) |
| `tests/conformance/py/conformance_match.py` | create | Python matcher + normalizer + variable substitution |
| `tests/conformance/py/01_matcher_vectors.py` | create | Runs every vector through the Python matcher |
| `tests/conformance/commands/README.md` | modify | Add the "Matchers and normalization" section (rules below) |
| `sources.cmake` | modify | Append `01_matcher_vectors.py` to `CONFORMANCE_PY_SOURCES` |

## Classes and functions

**Normalization (applied to the *actual* envelope before matching):**

| Rule | Effect |
|---|---|
| N1 | Delete `error.message` and any `error.details`. |
| N2 | Object key order ignored (structural compare). |
| N3 | Numbers compared by value (`1` equals `1.0`); booleans never equal numbers. |
| N4 | Envelope `id`: if request had `id`, actual must equal it (when transport `carries_id`); if request had none, actual must match `^srv-[0-9]+$`; after the check the value is replaced by `"<id>"` so `expect` may omit it. |
| N5 | A response with extra top-level keys beyond `v,id,ok,result|job|error` fails regardless of `match`. |
| N6 | Strings are NFC-compared byte-wise; no trimming. |

**Matcher tokens** (whole-string values in `expect`; tokens are the only wildcard mechanism):

| Token | Matches |
|---|---|
| `<any>` | any JSON value, including `null` |
| `<id>` | non-empty string, <= 64 chars, `[A-Za-z0-9_.:-]+` (link ids, job ids `j-<n>`, `srv-<n>`) |
| `<ts>` | RFC 3339 UTC string `YYYY-MM-DDThh:mm:ss[.fff]Z` |
| `<int>` / `<uint>` | integer / integer >= 0 |
| `<slug:N>` | string of exactly N chars from the slug alphabet (generated slugs; N = configured generated length, default from `SHORTENER_GENERATED_SLUG_LENGTH`) |
| `<regex:PATTERN>` | full-match of an ECMAScript-compatible regex (the dialect common to Python `re` and `std::regex`) |
| `<var:name>` | equals the value saved earlier as `name` |
| `<absent>` | key must not be present (used with `match: exact` to assert absence of a field is explicit) |

`${name}` substitution is applied to every **string inside request and expect** before use; `${tmp}` expands to the case temp dir (with `/` separators on all OSes). A literal `<` string that is not a token is exact text.

Python API (`conformance_match.py`):

```python
def substitute(obj, variables: dict) -> object
def normalize_actual(envelope: dict, request: dict | None, carries_id: bool) -> tuple[dict, list[str]]  # (normalized, id-check findings)
def match(expected, actual, mode: str, variables: dict, path: str = "$") -> list[str]  # [] = match; else "path: reason"
def jsonpath_get(doc, path: str)  # supports $.a.b[0].c only
```

**Vector file format:**

```json
[
  { "name": "ts_ok", "mode": "exact", "expected": {"created_at": "<ts>"},
    "actual": {"created_at": "2026-10-19T10:14:00Z"}, "match": true },
  { "name": "extra_key_exact_fails", "mode": "exact", "expected": {"a": 1},
    "actual": {"a": 1, "b": 2}, "match": false, "reason_contains": "$.b" }
]
```

Optional vector keys: `variables`, `request`, `carries_id`, `normalize` (bool: run N1-N6 on `actual` first).

## Implementation steps

1. Write the rules into `README.md` ("Matchers and normalization") first; reviewers sign off on text before code.
2. Create vectors: at least 3 per token (match, mismatch, edge), 4 for N4, 2 for `subset`, 2 for `<var:>`, 2 for `<absent>`, 3 for substitution (`${tmp}` with backslashes, nested, unknown var -> error).
3. Implement `conformance_match.py`; `jsonpath_get` rejects filters/wildcards.
4. `01_matcher_vectors.py` runs each vector as a `subTest`.

## Commands

```bash
python tests/conformance/py/01_matcher_vectors.py
ctest --test-dir cmake-build -R "^conformance_01_matcher_vectors$" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/py/01_matcher_vectors.py` | `conformance_01_matcher_vectors` | `integration;conformance` | one subTest per vector (>= 60) |

## Constraints

- Regex dialect restricted to constructs valid in both Python `re` and `std::regex` ECMAScript (no lookbehind, no named groups, no possessive quantifiers); vectors include a lint check that rejects such patterns.
- Tokens never match object keys, only values.
- No locale-dependent behavior (timestamps are ASCII, UTC only).

## Success criteria

1. Every token and rule N1-N6 is exercised by at least one passing and one failing vector.
2. The vector file is the only normative source: `README.md` links to it, and subtask 03 consumes the same file unchanged.
3. Unknown token-looking strings (`<foo>`) are treated as exact text, covered by one vector.

## Out of scope / risks

- Risk: two implementations (Python, C++). Mitigation is the shared vector file; any rule change edits vectors first.
- Unordered-array matching is deliberately not supported; cases that return unordered collections must sort in the command (list ordering is defined by `sort`/`order`) or assert via `subset` on scalars.
