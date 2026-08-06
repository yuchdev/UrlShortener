# Task 07.04 - Update public docs and complete review gates

## Objective

Bring public documentation and review artifacts in sync after the refactor.

## Files to modify

- `README.md`
- `ARCHITECTURE.md`
- `.github/copilot-instructions.md`
- `docs/analytics.md`
- generated API reference file selected in Task 07.02

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

## Acceptance criteria

- Docs match the final routing architecture.
- Review findings are resolved or explicitly deferred.
- No generated build artifacts are tracked.

