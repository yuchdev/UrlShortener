# 04 - Update public docs and complete review gates

**Parent task:** 08.0 OpenAPI/docs cleanup
**State:** ✅ Complete
**Depends on:** 01, 02, 03
**Blocks:** none

## Objective

Bring public documentation and review artifacts in sync after the refactor.

## Files to modify

- `README.md`
- `ARCHITECTURE.md`
- `.github/copilot-instructions.md`
- `docs/analytics.md`
- generated API reference file selected in subtask 02 (`docs/api/README.md`)

## Documentation updates

Update:

- HTTP routing overview;
- canonical endpoint list;
- compatibility endpoint notes;
- redirect fast-path warning;
- build/test command examples if any target names changed;
- statement that C++ route metadata is the API source of truth.

## Review gates

Before marking the refactor complete:

- run `feature-reviewer`;
- run `security-auditor` if redirect or untrusted URL handling changed;
- run `docs-updater` for public docs;
- run `test-gap` for HTTP/router coverage.

## Validation

Required commands:

```powershell
cmake --build "C:\Users\atatat\Projects\UrlShortener\cmake-build" --target url_shortener --config Debug
ctest --test-dir "C:\Users\atatat\Projects\UrlShortener\cmake-build" -C Debug -L unit --output-on-failure
ctest --test-dir "C:\Users\atatat\Projects\UrlShortener\cmake-build" -C Debug -L "unit|contract|integration|e2e" --output-on-failure
```

## Constraints

- Docs match the final routing architecture.
- Review findings are resolved or explicitly deferred.
- No generated build artifacts are tracked.

## Success criteria

- [x] `README.md`, `ARCHITECTURE.md`, `.github/copilot-instructions.md`, and
      `docs/analytics.md` all exist in the repository and are current with
      the router-based architecture.
- [x] `docs/api/README.md` (the generated API reference from subtask 02) is
      checked in and reflects `registeredRoutes()`.
- [x] Security review completed for the overall route refactor and passed:
      [docs/security/2026-07-17-rest-api-route-refactor.md](/docs/security/2026-07-17-rest-api-route-refactor.md)
      - Verdict: PASS. The redirect-specific review also passed (Task 06.0,
      subtask 03).
- [x] Docs match the final routing architecture (one-line
      `handleShortenerRequest()` delegation, router-backed dispatch).
- [x] Review findings are resolved or explicitly deferred - the one
      non-blocking finding in the security review (URI-store bounding/
      eviction) is explicitly logged as a follow-up, not blocking.
- [x] No generated build artifacts are tracked (verified: `cmake-build/` is
      not part of the tracked tree).
