# 0001 REST API Refactoring Specification

Status: Draft for review  
Parent plan: `docs/specs/rest-api-refactoring.md`

## Purpose

This directory breaks the REST API refactoring plan into implementation-ready
atomic tasks. Each stage is intentionally small enough to review and revert
independently. Testing is part of the stage work; there is no separate testing
stage.

## Stage directory map

| Stage | Directory | Goal |
|---:|---|---|
| 00 | `00-baseline-characterization` | Capture current behavior before routing changes. |
| 01 | `01-router-infrastructure` | Add small router primitives without changing production dispatch. |
| 02 | `02-observability-routes` | Move health/readiness/metrics through the router first. |
| 03 | `03-link-management-routes` | Extract and route canonical `/api/v1/links` handlers. |
| 04 | `04-compatibility-routes` | Preserve and route `/api/v1/short-urls` aliases. |
| 05 | `05-redirect-routes` | Extract redirect handlers while protecting the fast path. |
| 06 | `06-fallback-routes` | Route generic URI-store fallback behavior last. |
| 07 | `07-openapi-docs-cleanup` | Extend metadata, generate docs, and remove obsolete routing code. |

## Global invariants

- Keep `src/http/http_session.cpp` session lifecycle unchanged.
- Keep `handleShortenerRequest()` as the public entry point until cleanup.
- Preserve current response status codes and JSON/text body shapes unless a task
  explicitly says otherwise.
- Keep `/r/{slug}` and root `/{slug}` redirect handling narrow and free of
  management-plane logic.
- Do not add an external REST framework.
- Do not add a JSON dependency in these tasks.
- Use C++17 only.
- Register every new `.cpp` in `sources.cmake` and, when needed, in
  `CMakeLists.txt`.

## Validation baseline

Use a normal repository-local build directory, not a CLion temp directory:

```powershell
cmake --build "C:\Users\atatat\Projects\UrlShortener\cmake-build" --target url_shortener --config Debug
ctest --test-dir "C:\Users\atatat\Projects\UrlShortener\cmake-build" -C Debug -L unit --output-on-failure
```

