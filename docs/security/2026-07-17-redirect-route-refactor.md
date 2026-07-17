# Threat Model - Redirect Route Refactor (RedirectHandlers + RouterBuilder) - 2026-07-17

Scope: the new router-based redirect slice extracted from `handleShortenerRequest`.

Files reviewed:
- `include/url_shortener/http/handlers/RedirectHandlers.hpp`
- `src/http/handlers/RedirectHandlers.cpp` (`handlePrefixedRedirect`, `handleRootRedirect`,
  `emitClickEvent`, `redirectResponse`, `incrementRedirectStats`, `linkStateError`)
- `src/http/RouterBuilder.cpp` (registration of `GET /r/{slug}`, `GET /{slug}`, fallbacks)
- `src/http/Router.cpp` / `include/url_shortener/http/RouteContext.hpp` (matcher, path params)
- `src/http/request_handlers.cpp` (`handleShortenerRequest` dispatch, `applicationRouter()`)
- `tests/unit/http/12_prefixed_redirect_handler.cpp`, `13_root_redirect_handler.cpp`

Build validation: configured and built with the README Windows Visual Studio
workflow, then built and ran the targeted HTTP CTest selection for redirect,
fallback, endpoint-matrix, router-consistency, and compatibility handlers.
Findings are from static review + comparison against the pre-change `HEAD`
implementation (`git show HEAD:src/http/request_handlers.cpp`) plus the targeted
validation run.

## Assets & trust boundaries

- **Untrusted inputs:** request target (slug + query string), HTTP method, `Host`,
  `Referer`, `User-Agent`, `X-Forwarded-For`.
- **Trust boundary crossed:** target → Router matcher (`splitPath`/`matchPath`) →
  `isValidSlug` validator → link store (`getLinkForRead`) → `Location` response header /
  analytics click event.
- **Sensitive assets:** stored `target_url` (open-redirect surface), analytics client-hash
  salt (`config.analytics_client_hash_salt`), link stats, request id.
- **Key control location (unchanged):** the SSRF/open-redirect gate
  (`isPrivateHost` + `shortener_allow_private_targets`) lives in
  `validateAndNormalizeUrl` at **link-creation** time (`src/core/utils.cpp:237`). Redirect
  handlers only reflect the already-validated stored `target_url`; they perform no outbound
  fetch (server does not connect to the target — this is browser-driven redirection, not
  server-side request forgery).

## Findings

### [INFO] Open-redirect surface is inherent and behavior-preserved (CWE-601)
- Vector / evidence: `RedirectHandlers.cpp:104-114` (`redirectResponse` sets
  `bhttp::field::location = link.target_url`). Identical to pre-change
  `HEAD:request_handlers.cpp:969,998`.
- Impact: A shortener is an intentional open redirector; the target is attacker-suppliable at
  create time. Private/loopback/link-local/metadata targets are rejected at creation by
  `isPrivateHost` unless `shortener_allow_private_targets` is set. No re-validation on
  redirect existed before and none is expected here.
- Mitigation: None required for this slice — the create-time gate is unchanged. (Future
  hardening: optional egress re-check if targets can be mutated post-creation.)

### [INFO] No header/CRLF injection via Location (CWE-113)
- Vector / evidence: slug is constrained by `isValidSlug`
  (`^[A-Za-z0-9][A-Za-z0-9_-]{2,63}$`, `src/core/utils.cpp:135`) — no CR/LF/`?`/`/`.
  `target_url` is validated/normalized at creation. Only these two values reach response
  headers.
- Impact: No response-splitting vector introduced.
- Mitigation: None required.

### [INFO] Redirect guard parity is explicit
- Vector / evidence: both `handlePrefixedRedirect` and `handleRootRedirect` call
  `passwordGuardCheck` + `rateLimitGuardAllow` before stats updates and redirect responses.
- Impact: The root alias cannot bypass future password/rate-limit policy hooks while the
  prefixed route enforces them.
- Mitigation: None required for this slice.

### [INFO] Analytics salt is not disclosed
- Vector / evidence: `analytics_client_hash_salt` is used only as the HMAC-SHA256 key in
  `hashClientIdentifier` (`src/core/utils.cpp:293`); only the hex digest is persisted.
  `X-Forwarded-For`/`Referer`/`User-Agent` are clamped (128/512). No salt, token, DSN, or
  key is written to logs, response bodies, or error messages in the reviewed paths.
- Impact: None.
- Mitigation: None required.

## Behavior-preservation verification (route order / fallback / query handling)

- **Route ordering:** `/r/{slug}` and `/{slug}` are registered before the `/{path}` and `/`
  stubs. `/{slug}` and `/{path}` (GET) have equal specificity 0; `Router::dispatch` selects
  the first matching route in insertion order, so `handleRootRedirect` owns the GET fallback
  and delegates non-slug/non-active cases to `handleApplicationRequest`. No fallthrough
  auth/route bypass.
- **Method handling:** wrong-method on `/r/{slug}` now yields the router's `400
  invalid_method` (confirmed by test 12); previously a bespoke 400. Same status/class.
- **Query strings:** `/r/{slug}?x` uses the router-captured slug and redirects like
  `/r/{slug}`; `/{slug}?x` falls through to the URI-store fallback.
- **API guard:** `/api/...` targets are excluded from the root-redirect branch
  (`request_handlers.cpp:493` `rfind("/api/",0)!=0`; test `api_targets_are_not_handled...`).
- **Terminal states:** deleted→404, disabled/expired→410 preserved (tests 12 & 13).

## Verdict: PASS

No exploitable, change-introduced security issue was found. The SSRF/open-redirect gate
remains at create time, slug validation is unchanged and strict, no new header-injection or
secret-disclosure surface is introduced, and route ordering does not create an auth/route
bypass. Guard enforcement is now symmetric for prefixed and root redirects.
