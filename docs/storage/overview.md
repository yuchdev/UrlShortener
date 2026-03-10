# Storage Abstraction Overview (Stage 03 Scope 1)

This document captures the Stage 03 interface-extraction slice implemented in this repository.

## What was introduced

- `IMetadataRepository` as the metadata storage port used by link flows.
- `ILinkCacheStore` as the cache port used by redirect read paths.
- In-memory adapters for both ports (`InMemoryMetadataRepository`, `InMemoryLinkCacheStore`).
- Cache-aside read helper for slug resolution:
  1. cache lookup by slug
  2. fallback to metadata repository
  3. cache populate on miss
- Deterministic cache invalidation on link updates (`updateLinkAndInvalidateCache`).

## Boundaries

- HTTP route handlers still own transport parsing/response mapping.
- Route logic now depends on storage ports rather than concrete backend state where link lookups/updates occur.
- Backends remain in-memory in this scope; SQLite/PostgreSQL/Redis are not part of this slice.

## Redirect fast-path notes

The redirect path remains intentionally narrow:

1. parse slug
2. read link through cache-aside helper
3. evaluate lifecycle state
4. return redirect or terminal response
5. persist counters and invalidate cache

No additional blocking side-work was introduced into redirect completion.

## Out of scope for this scope

- External backend adapters (SQLite/PostgreSQL/Redis)
- Runtime configuration-based adapter selection
- Multi-backend contract matrix
