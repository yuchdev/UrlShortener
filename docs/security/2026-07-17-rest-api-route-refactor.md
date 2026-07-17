# Threat Model - REST API Route Refactoring (Router/RouteRegistry/Handlers) - 2026-07-17

Scope: the router-based dispatch refactor that replaces the monolithic
`handleShortenerRequest` prefix-matching chain with a data-driven `Router`,
`RouteRegistry`, `RouterBuilder`, and per-concern handler translation units.
This complements the narrower slice review in
`docs/security/2026-07-17-redirect-route-refactor.md`.

Files reviewed:
- `src/http/Router.cpp` / `include/.../Router.hpp` (matcher, specificity, dispatch)
- `src/http/RouteRegistry.cpp` (route inventory, doc metadata, `routeLabelForTarget`)
- `src/http/RouterBuilder.cpp` (route registration + error handler wiring)
- `src/http/handlers/{Redirect,Fallback,Link,Compatibility,Observability}Handlers.cpp`
- `src/http/request_handlers.cpp` (`handleShortenerRequest` -> `applicationRouter().dispatch`,
  `handleApplicationRequest` URI-store fallback)
- `tools/route_registry_dump.cpp` + generated `docs/api/README.md`
- `src/core/utils.cpp` (`normalizeTargetUrl` / `isPrivateHost` SSRF gate, `isValidSlug`)
- `src/http/http_session.cpp` (body-size limit)

Method: STRIDE-lite static review + differential comparison against pre-change
`HEAD:src/http/request_handlers.cpp` to isolate change-introduced risk from
inherited/pre-existing behavior.

## Assets & trust boundaries

- **Untrusted inputs:** request target (path + query), HTTP method, request body,
  `Host`, `Referer`, `User-Agent`, `X-Forwarded-For`.
- **Boundary crossed:** target -> `Router::matchPath` (splitPath / capture / specificity)
  -> per-route handler -> validator (`isValidSlug`, `normalizeTargetUrl`) -> link store /
  URI-store -> `Location` header / JSON response / analytics click event.
- **Sensitive assets:** stored `target_url` (open-redirect surface), analytics
  client-hash salt (`config.analytics_client_hash_salt`), link metadata/stats.
- **Key control location (unchanged):** SSRF/open-redirect gate
  (`isPrivateHost` + `shortener_allow_private_targets`) lives in `normalizeTargetUrl`
  (`src/core/utils.cpp:198-238`), invoked at link-creation time only. Redirect handlers
  reflect the already-validated stored `target_url`; no server-side outbound fetch exists.

## Findings

### [INFO] Wildcard fallbacks cannot steal `/api`, `/r`, or observability routes (confidence: HIGH)
- Vector / evidence: `Router::dispatch` (`Router.cpp:120-165`) selects the matching route
  with the highest `pathSpecificity` (`Router.cpp:73-82` = count of non-capture segments),
  independent of registration order. `/{path}` and `/{slug}` have specificity 0; `/r/{slug}`
  =1; `/api/v1/links/...` = 3-5; `/healthz|/readyz|/metrics` = 1. Any concrete route
  therefore out-ranks the wildcard fallbacks. The greedy `/{path}` special case
  (`Router.cpp:213-224`) only triggers on a segment-count mismatch and still carries
  specificity 0, so it never displaces a concrete match.
- Impact: No route/auth bypass; create/redirect/observability handlers are reached with
  their own validation intact.
- Mitigation: None required. Regression coverage already exists
  (`tests/unit/http/02,04,08`); recommend keeping the specificity assertions.

### [INFO] SSRF / open-redirect gate preserved (CWE-918 / CWE-601) (confidence: HIGH)
- Vector / evidence: create path (`LinkHandlers.cpp:238`) still calls
  `normalizeTargetUrl(*raw_url, config)` which enforces `isPrivateHost` +
  `shortener_allow_private_targets`. `handlePatchLink` (`LinkHandlers.cpp:532-660`) never
  reads a `"url"` field, so `target_url` is immutable post-creation - no SSRF-on-update
  path. Redirect handlers set `Location` from the stored, already-validated `target_url`
  (`RedirectHandlers.cpp:104-107`) and perform no outbound connection.
- Impact: SSRF/open-redirect surface unchanged from pre-refactor baseline.
- Mitigation: None required.

### [INFO] No header/CRLF injection into `Location` (CWE-113) (confidence: HIGH)
- Vector / evidence: capture segments reject empty values (`Router.cpp:236-241`); slugs
  reaching redirect are re-checked by `isValidSlug` (`^[A-Za-z0-9][A-Za-z0-9_-]{2,63}$`),
  which excludes CR/LF/`?`/`/`. `target_url` is normalized at creation.
- Impact: No response-splitting vector introduced.

### [INFO] Analytics salt and secrets not disclosed (confidence: HIGH)
- Vector / evidence: `analytics_client_hash_salt` is used only as the HMAC key in
  `hashClientIdentifier` (`RedirectHandlers.cpp:71`); only the digest is persisted.
  `logStructured` runs values through `sanitizeLogValue` (`request_handlers.cpp:57,279-287`).
  No salt/token/DSN/key is emitted to logs, bodies, or error messages in the reviewed paths.
- Impact: None.

### [INFO] Generated API docs expose no runtime secrets/endpoints (confidence: HIGH)
- Vector / evidence: `tools/route_registry_dump.cpp` renders only
  `method | path_pattern | operation_id | tags | markers | summary` from
  `registeredRoutes()`. `docs/api/README.md` contains route *patterns* (e.g. `/{slug}`),
  not runtime hostnames, `shortener_base_domain`, DSNs, or credentials.
- Impact: None.

### [LOW] Root-redirect no longer falls through to URI-store for a valid-but-unknown slug (confidence: MEDIUM)
- Vector / evidence: pre-change, `GET /{slug}` with a valid slug and no link fell through
  to `handleApplicationRequest` (URI-store) at `HEAD:request_handlers.cpp` tail. New
  `handleRootRedirect` (`RedirectHandlers.cpp:230-236`) returns `404 not_found` via
  `linkStateError` instead of consulting the URI-store.
- Impact: Behavioral change only; it *reduces* information exposure (no accidental
  URI-store read via a slug-shaped key). Not exploitable.
- Mitigation: None required; confirm with product owner this is intended.

### [LOW] Pre-existing: no authN/authZ on mutating `/api/v1/links` routes (CWE-306) (confidence: HIGH)
- Vector / evidence: `POST/PATCH/DELETE /api/v1/links/{slug}` and `enable/disable/restore`
  are dispatched with no auth check in either the old or new code path
  (`RouterBuilder.cpp:41-52`). Any unauthenticated caller can create, mutate, disable, or
  soft-delete links and read stats.
- Impact: Spoofing / elevation on link lifecycle. **Pre-existing and unchanged by this
  refactor** - not introduced here, so it does not block this change, but it is the single
  highest-value hardening item for the service and should be tracked.
- Mitigation: Add an authenticated management surface (API key / bearer) before exposing
  these routes to untrusted networks. Hand to `cpp-expert` as a follow-up, not part of this
  merge gate.

### [LOW] Pre-existing: unbounded in-memory URI-store on `POST /{path}` (CWE-770) (confidence: MEDIUM)
- Vector / evidence: `handleFallbackUriStore` -> `handleApplicationRequest`
  (`request_handlers.cpp:448-453`) stores request bodies keyed by arbitrary path in a
  process-lifetime singleton. Per-request body size is capped
  (`http_session.cpp:59,151` via `max_request_body_bytes`), but total entries are unbounded.
- Impact: Memory-exhaustion DoS by an attacker POSTing many distinct paths. Pre-existing;
  the refactor documents the fallback routes but does not widen the attack surface.
- Mitigation: Bound/evict the URI-store or gate it behind config. Follow-up, not blocking.

## Behavior-preservation verification
- Method mismatch on a concrete path (e.g. `GET /api/v1/links`) returns router
  `400 invalid_method` scoped to the best-specificity route set, not a wildcard fallthrough
  (`Router.cpp:143-176`) - create-time validation is never bypassed.
- Trailing-slash oddities (e.g. `POST /api/v1/links/`) fall to the URI-store echo, which
  never creates a redirecting link, so the SSRF create-gate cannot be sidestepped.
- Guard parity: both prefixed and root redirects call `passwordGuardCheck` +
  `rateLimitGuardAllow` before stats/redirect (`RedirectHandlers.cpp:188-189,229-230`).
- Terminal link states (deleted->404, disabled/expired->410) preserved
  (`RedirectHandlers.cpp:linkStateError`).

## Verdict: PASS

No exploitable, change-introduced security issue was found. Router specificity prevents
wildcard fallbacks from hijacking `/api`, `/r`, or observability routes; the SSRF/open-redirect
gate and strict slug validation are preserved; `target_url` is immutable on PATCH; no new
header-injection or secret-disclosure surface is introduced; and generated docs expose only
route patterns. Two LOW items (`missing management authN/authZ`, `unbounded URI-store`) are
**pre-existing** and unchanged by this refactor - they do not block this merge but should be
tracked as hardening follow-ups (route to `cpp-expert` + `testing-expert`).
