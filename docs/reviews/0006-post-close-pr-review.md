# PR review: milestone/0006-unified-command-model-and-tui (post-close follow-ups)

**Merge verdict: APPROVE**

- Range: `2d17b3e..5bec4ad`. These are the follow-ups to the open decisions
  recorded at the milestone close.
- The range excludes `e30f173`, a notes-only commit to
  `docs/roadmap/0007-code-improvements/plan.md`.
- Reviewers: `feature-reviewer` and `security-auditor`, run in parallel. Both
  returned a verdict.
- Full suite on `b9fb70b`, labels `unit|contract|integration|e2e|conformance`,
  run with `-j8`: **482/482**.
  - `ctest` registers 487 tests in total. Five carry none of those labels: the
    four perf tests and `hiredis-test`.
  - No test was added or removed in this range.

## Commits reviewed

| Commit | Change |
|---|---|
| `99884fa` | `parseRfc3339Zulu` becomes strict and calendar-aware (see the note below) |
| `166deea` | `SanitizeField` escapes control bytes in log fields (CWE-117): CR becomes `\r`, LF becomes `\n`, and other C0 bytes and DEL become `\xHH`. Truncation never splits an escape. |
| `2810b1b` | Usage errors in every mode raise a dedicated `UsageError` and exit 2 with `error: ...` on stderr, with no FATAL line. The diagnostic is sanitized. |
| `b9fb70b` | CLI integration tests 11-16 expect exit 2 for usage errors; not-found cases still exit 1 |
| `1dbce13` | Nine CLI sugar entries that do not exist are removed from descriptor `maps_to`; the catalog section 3.7 is regenerated |
| `ebebf62` | The strict timestamp grammar is documented in `docs/commands/README.md` |
| `c42cd1e` | A method mismatch on a known path returns 405 with an `Allow` header; the body keeps `invalid_method` |
| `3ec0466` | Test timeouts scale under sanitizer builds via `URLSHORTENER_TEST_TIMEOUT_SCALE`. The TSan runs of conformance 10/11/13/14 now pass. |
| `4a09293` | The Postgres contract harness uses one schema per process |
| `d82599b` | GitHub Actions are pinned to commit SHAs |
| `5bec4ad` | Two stale comments flagged by this review are corrected |

The strict timestamp grammar from `99884fa`:

- Exactly `YYYY-MM-DDTHH:MM:SS`.
- Optionally `.` followed by 1-9 fractional digits.
- A mandatory `Z`.
- Gregorian leap-day validation.
- Leap second `:60` rejected.

## Feature review (feature-reviewer)

**LGTM.** No blocking issues.

Non-blocking items:

- **Fixed in `5bec4ad`:**
  - The comment at `src/cli_parser.cpp:44` still claimed `SanitizeField` does not escape CR/LF.
  - A misleading destruction-order comment in `metadata_contract_common.hpp`.
- **Open:**
  - **Sanitizer detection.** It reads only `CMAKE_CXX_FLAGS`. Sanitizers injected through per-config flags or `add_compile_options` fall back to a scale of 1. This is safe: the tests just keep their base timeouts.
  - **Year width.** Add one explicit negative case for a timestamp year that is not 4 digits.
  - **Gated-off 405 path.** The gated-off command-API 405 path (`Allow: POST`) has no dedicated conformance case.

## Security review (security-auditor)

**PASS.** Nothing CRITICAL, HIGH or MEDIUM.

- **Log fields.** `SanitizeField` escaping is complete for field values, and atomic truncation never splits an escape. UTF-8 sequences cannot carry a raw CR or LF.
- **Usage errors.** No raw argv reaches stderr or the log, including Boost.ProgramOptions `what()` text.
- **Timestamp parser.** It cannot overflow. A parse failure rejects `expires_at`; it never disables expiry.
- **405 response.** The `Allow` value is built only from registered route verbs, so it cannot carry header injection. The gated-off route discloses nothing beyond `Allow: POST`.
- **Postgres isolation.** The schema name is numeric or hex only, so it is safe to interpolate into SQL.
- **SHA pins.** All three were verified against the GitHub API, and no floating tags remain.

LOW, informational, and pre-existing:

- Log `msg` and field keys bypass `SanitizeField`. This is safe today because every call site uses static text.
- C1 bytes `0x80`-`0x9F` are not escaped in log values.

## Combined blocking items

None.
