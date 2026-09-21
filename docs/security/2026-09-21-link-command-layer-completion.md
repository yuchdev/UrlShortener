# Threat Model - Link command-layer completion (Milestone 0003 Task 01.0) - 2026-09-21

Scope: `git diff master...milestone/0003-cli-rest-interfaces`, five commits
(3853319, 05598c9, c3d7684, a48b887, d99f0c6). Refactor of the PATCH / DELETE /
enable / disable / restore / preview REST handlers in
`src/http/handlers/link_handlers.cpp` onto `app::LinkCommandService`
(`src/app/link_command_service.cpp`, `src/app/legacy_adapters.cpp`). No product
code modified by this review. Uncommitted working-tree changes (.claude/, docs/,
[README.md](/docs/security/README.md), CMakeLists.txt) are out of scope.

## Assets & trust boundaries

- Untrusted input: PATCH JSON body (`enabled`, `expires_at`, `tags`,
  `metadata`, `campaign`) and the `slug` path parameter.
- Trust boundary crossed: HTTP request -> handler shape-validation -> command
  DTO -> `LinkCommandService` business-validation -> repository/cache mutation
  -> serialized JSON response.
- Assets: stored link records (soft-delete state, expiry, target URL), cache
  coherence of the redirect fast path.
- Note: the URL target is NOT mutable through PATCH in either the old or new
  code (no `url`/`target_url` field is parsed, and `UpdateLinkCommand` has no
  such field), so there is no new SSRF surface on the update path. The create
  path's SSRF guard (`normalizeTargetUrl` / `isPrivateHost`) is unchanged.

## STRIDE-lite

| Category | Assessment |
|---|---|
| Spoofing/Auth | No authz change. Handlers carry no `AccessGuard` calls before or after the refactor; the local-only auth model is enforced elsewhere and untouched. |
| Tampering/Injection | Same manual JSON scanners; same `validateTags`/`validateMetadata`/`validateCampaign`/`parseRfc3339Zulu` guards, now enforced in BOTH handler and service. No shell/SQL/WebView sink introduced. |
| Repudiation | No audit-logging change; parity with prior handler. |
| Info disclosure | Error details generic ("Link not found", "repository"); no secrets/PII/internals in responses or logs. No new logging sinks. |
| DoS | No new unbounded input; body size still bounded by request hardening; validators enforce the same tag/metadata/campaign limits. |
| Elevation | None; no privileged/remediation flow added. |

## Findings

### [LOW] PATCH error precedence changed: shape-400 now beats existence-404
- Vector / evidence: `src/http/handlers/link_handlers.cpp:415` (`handlePatchLink`
  now shape-validates the body into `UpdateLinkCommand` before the existence
  lookup, which moved into `LinkCommandService::UpdateLink`,
  `src/app/link_command_service.cpp:146`). Old code checked existence first
  (`git show 3853319^:src/http/handlers/link_handlers.cpp`, `getLinkForRead`
  before body parsing).
- Impact: For a nonexistent slug plus a malformed body, the response flips from
  `404 not_found` to `400 invalid_*`. This contradicts the milestone's
  "REST behavior unchanged" claim. Security impact is neutral-to-positive: the
  old ordering was a minor existence oracle (404 vs 400 under a malformed body);
  the new ordering removes it. Valid-body requests are unaffected (still 404 for
  missing, 200 otherwise). Non-blocking.
- Mitigation: Accept as an intentional (benign) contract nuance and document it,
  or, to be strictly behavior-preserving, have `handlePatchLink` resolve the
  slug before body validation. Recommend a characterization test asserting the
  chosen precedence in `tests/unit/http/10_link_handlers.cpp`.

### [INFO] Duplicate validation is the correct defensive posture for the CLI path
- Evidence: handler retains `validateTags`/`validateMetadata`/`validateCampaign`
  /RFC3339 checks; `LinkCommandService::UpdateLink` re-runs the same validators
  (`src/app/link_command_service.cpp:168-195`). The REST-specific per-field 400
  codes (`invalid_enabled`/`invalid_expires_at`/`invalid_tags`/`invalid_metadata`
  /`invalid_campaign`) are produced by the handler and reached first, so REST
  error codes are preserved; the service is the sole mutation path, so the
  forthcoming CLI caller (Task 02.0/03.0) inherits identical business validation
  with no bypass. No action required.

## Verified parity

- Cache invalidation preserved on every mutating path: `UpdateLink`,
  `DeleteLink`, `SetLinkEnabled`, `RestoreLink` all call `store_.update()`, and
  `LegacyLinkStore::update` (`src/app/legacy_adapters.cpp:78`) is
  `updateLinkAndInvalidateCache`. No stale-redirect regression.
- Soft-delete semantics identical: both old handler and
  `LegacyLinkStore::findBySlug` resolve via the same `getLinkForRead(slug)`; a
  future CLI caller mutating/previewing a soft-deleted link behaves exactly as
  the old REST code did.
- Redirect fast path untouched: no redirect/router/link_service file appears in
  the diff.
- Scoped tests pass: `ctest -R "10_link_handlers|app__"` -> 17/17 PASS.

## Verdict: PASS_WITH_FOLLOWUP

No CRITICAL/HIGH findings. Merge is not blocked. Follow-up: decide and pin the
PATCH not_found-vs-invalid-body precedence (LOW) with a characterization test.

## Round 2 (2026-09-21) - delta re-review: commits 3c86fa9, 49a6e96

Scope: only the two commits added since round 1. No product code modified by
this review.

### Round-1 LOW: RESOLVED

`handlePatchLink` now resolves existence via `LinkCommandService::GetLink`
BEFORE any body shape-validation (`src/http/handlers/link_handlers.cpp:429-436`),
restoring the master precedence: a PATCH to an unknown slug returns
`404 not_found` even with a malformed body. The round-1 existence-oracle nuance
(400-beats-404) is eliminated. New characterization test in
`tests/unit/http/10_link_handlers.cpp` pins this. The INFO validator-duplication
note is now documented in-code at `src/app/link_command_service.cpp:151-155`
(intentional defense-in-depth for the non-REST/CLI path); no change needed.

### New surface from the extra existence lookup: no new finding

- Oracle consistency: the pre-check `GetLink` and the authoritative `UpdateLink`
  both resolve through the identical `store_.findBySlug`
  (`src/app/link_command_service.cpp:136` and `:146`), so soft-delete visibility
  and not_found semantics match on both lookups. A soft-deleted link that
  `UpdateLink` would mutate also passes the pre-check (parity with master); a
  hard-missing slug fails both. No divergence, no new soft-deleted-link exposure.
- TOCTOU / double-lookup: nothing security-relevant. If a link disappears
  between the pre-check and `UpdateLink`, `UpdateLink` re-checks existence and
  returns not_found; if it appears, the pre-check 404s harmlessly. Body/business
  validation always runs in both handler and service regardless of the pre-check
  outcome, so no path skips validation. The pre-check is advisory, not
  authoritative.
- Timing: the added lookup is a single in-store read; no meaningful timing
  oracle beyond what master already exposed via its own existence check.
- `DeleteLink` single-timestamp change
  (`src/app/link_command_service.cpp:214-216`): one `currentTimestamp()` for both
  `deleted_at` and `updated_at`. Cosmetic/consistency only; no security impact.

### Commit 3c86fa9 - sqlite_state_assert.py `ps` fallback (test tooling): no finding

- `_assert_process_running_via_ps` invokes `ps` with a FIXED argv
  (`["ps", "-axo", "pid=,command="]`), `shell=False`, no interpolation of any
  caller-controlled value. The `name` argument is used only as a Python
  substring test (`name in command`) against `ps` output - it never reaches the
  subprocess, a shell, or a query. No command/argument injection (CWE-78) is
  reachable. Errors are caught and returned, not raised. This is POSIX-only e2e
  test tooling outside the shipped binary; substring matching could in theory
  false-positive on an unrelated process, but that is a test-correctness concern,
  not a security one.

### Round 2 verdict: PASS

No CRITICAL/HIGH/MEDIUM/LOW findings in the delta. The round-1 LOW is resolved
and the round-1 INFO is documented in-code. Scoped tests
`ctest -R "10_link_handlers|app__"` -> 17/17 PASS. Merge is not blocked.
