# 03 - Documentation

**Parent task:** 04.0 Tests, CI, and Documentation
**State:** ⬜ Not started
**Depends on:** 01.0-03.0
**Blocks:** none

## Objective

Write `docs/web-app/` documenting local development, the `/app` static-hosting
contract, testing, and how this public UI relates to 0002's `/admin` console
and the existing `/api/v1/links` API. Also add the CI wiring for the frontend.

## Files to add

```text
docs/web-app/README.md
docs/web-app/local-development.md
docs/web-app/static-hosting.md
docs/web-app/testing.md
.github/workflows/web-app.yml
```

## Requirements

1. `README.md` — what the public web UI is (paste URL → short link → copy),
   its intentional thinness and V1 non-goals (see
   [plan.md SC6](/docs/roadmap/0004-web-ui/plan.md)), and how it relates to
   the other surfaces: it is **public/unauthenticated** at `/app`, distinct
   from 0002's login-gated analytics console at `/admin`, and it consumes only
   the existing public `/api/v1/links` API (create + preview).
2. `local-development.md` — running `web/app/` locally (`npm ci`, `npm run
   dev`, `npm run build`), the Vite `base: "/app/"` requirement, and how to
   run against the C++ backend serving the built assets.
3. `static-hosting.md` — the SC4 contract: the new static-assets handler, the
   `/app` + `/app/*` routing, why it must be mounted ahead of the redirect and
   fallback catch-alls, and what it must never shadow (`/{slug}`, `/api/*`,
   observability). Document the optional public-create rate limit (SC5): its
   config flags, that it is off/no-op by default and fail-open.
4. `testing.md` — how to run Vitest and Playwright, where the shared MSW fault
   catalogue lives, and the expectation that every fault mode is covered.
5. `.github/workflows/web-app.yml` — CI running frontend lint, typecheck,
   unit tests, build, and the Playwright e2e suite.
6. Documentation must be honest about the SSRF-opacity UX (SC3): a rejected
   URL is shown with one non-leaky message and the doc should not suggest the
   UI can explain *why* a URL was blocked.

## Constraints

- Docs only (plus the CI YAML); no product code.
- Use absolute-from-repo-root links per the roadmap linking convention.
- Do not claim 0002's `/admin` code exists — reference it as a separate,
  planned surface following the same static-hosting pattern.

## Success criteria

- [ ] `docs/web-app/` covers local dev, static-hosting/rate-limit contracts,
      testing, and the relationship to `/admin` and `/api/v1/links`.
- [ ] The CI workflow runs frontend lint/typecheck/unit/build and e2e.
- [ ] Docs state plainly that `/app` is public and unauthenticated and honor
      the SSRF-opacity messaging.
- [ ] All internal links use the absolute-from-repo-root convention.
