# 02 - Backend Static Hosting for `/app` and `/app/*`

**Parent task:** 01.0 Web App Shell, Static Hosting, and Shorten Flow
**State:** ⬜ Not started
**Depends on:** 01 (needs the built `web/app/dist/` layout to serve)
**Blocks:** none

## Objective

Add a new C++ static-assets handler that serves the built SPA under the
literal prefix `/app` and its nested asset paths `/app/*`, mounted ahead of
the redirect and generic-fallback catch-alls, without shadowing any existing
route. This is new backend work handed to `cpp-expert`.

## Files to add

```text
src/http/handlers/app_static_assets_handler.h
src/http/handlers/app_static_assets_handler.cpp
```

Plus wiring changes (existing files, handed to `cpp-expert`):

```text
src/http/router_builder.cpp   # register the handler ahead of redirect/fallback
sources.cmake                 # add the new .cpp so it is actually built
```

## Requirements

1. Serve the SPA under `/app` and all nested `/app/*` paths. `/app` and any
   unknown `/app/*` sub-path that is not a real asset must return
   `index.html` (SPA client-routing fallback); real hashed assets under
   `web/app/dist/` (e.g. `/app/assets/index-*.js`, `*.css`) are served with
   correct content types and cache headers.
2. Register the handler in `src/http/router_builder.cpp` **ahead of** the
   redirect handlers and the generic URI-store fallback, following the same
   ordering pattern 0002 documents for `/admin` (see
   [plan.md SC4](/docs/roadmap/0004-web-ui/plan.md)). The handler must claim
   only `/app` and `/app/*`.
3. It must never intercept or shadow `/{slug}`, `/r/{slug}`, `/api/*`, or the
   observability routes. Because `src/http/router.cpp` dispatches by
   `pathSpecificity` (literal-segment count), the literal `/app` already
   outranks the `/{slug}` catch-all; the dedicated handler exists specifically
   because that router's single-segment catch-all cannot express nested
   `/app/*` asset paths.
4. The location of the served directory (`web/app/dist/` or a configured
   asset root) must be configurable, consistent with how `ServerConfig`
   exposes runtime paths; do not hardcode an absolute path.
5. Follow the coding guardrails: no cross-layer leakage, no management-plane
   logic in the redirect fast path, and keep the handler minimal (serve
   static bytes; no link/business logic). Treat request targets as untrusted —
   reject path traversal (`..`) and never serve files outside the asset root.
6. Never log secrets; static hosting logs at most the served path at debug
   level.

## Constraints

- This handler does no link creation, no preview, no analytics — static bytes
  only.
- Path handling must be traversal-safe: a crafted `/app/../../etc/passwd`
  style target must resolve to a `404`/rejection, never escape the asset root.
- Do not modify the redirect fast path; only insert the new handler ahead of
  it in registration order.
- `cpp-expert` implements; `app-architect` (this milestone) only specifies.

## Success criteria

- [ ] `GET /app` returns the SPA `index.html`.
- [ ] `GET /app/assets/<hashed>.js` returns the built asset with a correct
      content type.
- [ ] `GET /app/anything/unknown` (not a real asset) returns `index.html`
      (SPA fallback), not a 404.
- [ ] `GET /{slug}`, `GET /r/{slug}`, `/api/*`, and observability routes are
      unaffected and still resolve to their existing handlers.
- [ ] A path-traversal target under `/app/*` cannot read files outside the
      asset root.
- [ ] The new `.cpp` is listed in `sources.cmake` and the project builds.
