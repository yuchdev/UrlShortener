# 04 - REST create through the registry

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 02 (bridge), 01 (args builder), 03.0/11 (`link.create` registered, parity gate)
**Blocks:** 06
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Make `POST /api/v1/links` and the compatibility `POST /api/v1/short-urls` build a `link.create` request and call
the registry. All REST-only behavior - field-name aliases, per-field 400 codes and their **order** - stays in
`createLinkFromRequest`, because it is part of the public REST contract; only the DTO construction and the
service call/mapping disappear.

## Dependencies

- Subtask 02 (`execute_resource_command`, `resource_response(…, 201)`), subtask 01 (`args_builder`), 03.0/07 (`link.create` with canonical arg `target_url`).
- Verified REST behavior to preserve (`createLinkFromRequest`, `src/http/handlers/link_handlers.cpp`; decision-log Q18):
  1. target: `url` first, else `target_url`; neither -> `400 invalid_url` "url is required".
  2. slug: `slug`; on the compat route (`CreateSlugFieldMode::slug_or_code`) also `code` when `slug` is absent; `code` is ignored on the canonical route.
  3. `redirect_type` not `temporary|permanent` -> `400 invalid_redirect_type`.
  4. `expires_at` not RFC 3339 UTC -> `400 invalid_expires_at`.
  5. `enabled` present but not a bool -> `400 invalid_enabled`.
  6. `tags` present but not a string array -> `400 invalid_tags`.
  7. `metadata` present but not a flat string object -> `400 invalid_metadata`.
  8. `campaign` present: `null` -> no campaign; not an object -> `400 invalid_campaign`; an object -> the six known string members, other keys ignored.
  9. `deleted_at` present -> `400 invalid_request` "deleted_at is server-managed".
  10. Unknown top-level keys are **ignored** (the registry would reject them; this is why args are rebuilt from known keys).
  11. Success: `201` + `serializeLinkViewJson`. Service errors: statuses/codes via the bridge (409 for `reserved_slug` and `slug_conflict`, 400 `invalid_url`/`invalid_slug`, 400 `invalid_request` for `invalid_field`, 500 `repository`).
- The checks above use the quirky text-scanning helpers (`extractJsonStringField` finds the first `"key"` anywhere in the body). They are the current contract and are reused unchanged; replacing them with `scan_object` would change behavior for hostile bodies and is out of scope.
- ArgSpec caps (03.0/07) are generous so the service, not the registry, stays the validator for oversized input.
- **Create rate limit (T-75, DoS; 2026-11-16 decision - ALWAYS ON by default):** `POST /api/v1/links` and the compat `POST /api/v1/short-urls` throttle creation via the existing `IRateLimiter` port (`include/url_shortener/storage/i_rate_limiter.hpp`: `Allow(key, limit, window, RateLimitError*)`). The throttle is **active out of the box**, independent of `StorageConfig.rate_limit`. Specifics:
  - **Limiter selection:** a **dedicated in-memory create limiter** (`InMemoryRateLimiter`, no external deps) is **constructed by default** and held by the process-wide `LinkBackendHandle`. **Exception:** when `StorageConfig.rate_limit.enabled && backend == "redis"`, the configured Redis limiter (`StorageAdapters::rate_limiter`) is used **instead** of the in-memory default (so a multi-process deployment shares one window). Any other `StorageConfig.rate_limit` state (disabled, or `backend == "inmemory"`) leaves the dedicated in-memory create limiter in force. The selected limiter is exposed as `LinkBackendHandle::createRateLimiter()`.
  - **Key = the client peer address string** from `RouteContext::peer` (the behavior-neutral plumbing this subtask delivers, below). **Never a header** - `X-Forwarded-For` / `Forwarded` are spoofable and are not read. An absent peer (e.g. a `remote_endpoint` error) yields no key, so that single request is not throttled (fail-open).
  - **Limit / window** come from the new `ServerConfig` fields (below) with safe defaults of **30 creates per 60 s per client address**; they are applied to `Allow(key, limit, window)` for *both* limiter choices. `link_create_rate_limit == 0` **disables** the throttle entirely (documented off switch).
  - **`Allow` semantics:** `allowed == false` **with no `error`** -> over limit -> **`429` `rate_limited`** (fixed body, before `execute`). `allowed == false` **with `error` set** (backend/precondition failure, e.g. Redis down) -> **fail open**: the request proceeds (consistent with the Redis fail-open convention and the `IRateLimiter` contract).
  - **Scope:** the redirect fast path is **not** throttled (C10); the CLI is **not** throttled. REST create has no `dry_run`; the `dry_run`-counts-as-a-real-create rule (a dry_run runs the full SSRF/validator, so it is not cheaper) is enforced on the generic endpoint in subtask 09.
  - **`rate_limited` is a REST transport-level code** (HTTP 429), emitted by `makeApiErrorResponse(429, "rate_limited", <fixed message>)` **before** `execute` - exactly like the 415 `unsupported_media_type` and the 404 route-gate in subtask 09, neither of which is a `CommandErrorCode`. It is therefore **not** added to the 01.0/02 command error table (that table is for command-result errors; transport-level pre-execute errors live in the adapter). C1/C9 are preserved: command-result error mapping still lives only in `command_bridge.cpp`; the throttle is a transport gate, not a command result.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `src/http/handlers/link_handlers.cpp` | modify | Rewrite the tail of `createLinkFromRequest` (after the per-field checks): **first** apply the create throttle (keyed on the client peer; over-limit -> `429` `rate_limited`, before building args/executing), then build args, `execute_resource_command`, `resource_response(..., 201)`. Remove `app::CreateLinkCommand` use and `makeCommandService` from this path. |
| `include/url_shortener/http/handlers/link_handlers.hpp` | modify | `createLinkFromRequest` gains a `const RouteContext&` parameter (for `context.peer`); `handleCreateLink` and `handleCompatCreateLink` forward their `context` (both already receive one, today ignored). This is the one signature change the throttle requires; the per-field REST contract is unchanged. |
| `include/url_shortener/core/config.hpp` | modify | Add `std::uint64_t link_create_rate_limit = 30;` and `std::uint32_t link_create_rate_window_seconds = 60;` (safe defaults: 30 creates / 60 s / client address). `link_create_rate_limit == 0` disables the throttle explicitly. Doxygen on both, including the disable switch. |
| `src/cli_parser.cpp` | modify | Declare `--link-create-rate-limit <N>` (default 30, `0` disables) and `--link-create-rate-window-seconds <N>` (default 60) in the request-hardening group; include in `--help` with the "0 disables" note. C3: additive flags, no existing flag/default changes. |
| `include/url_shortener/composition/link_backend_bootstrap.hpp`, `src/composition/link_backend_bootstrap.cpp` | modify | `InitLinkBackend` **always constructs** a dedicated `InMemoryRateLimiter` (using the injected `IClock`) for create throttling and stores it on `LinkBackendHandle`. Add `std::shared_ptr<IRateLimiter> createRateLimiter() const;` (analogous to `repository()`) returning the **selected** create limiter: `m_adapters_.rate_limiter` when `StorageConfig.rate_limit.enabled && backend == "redis"`, else the dedicated in-memory create limiter (never `nullptr`). No new process singleton. |
| `include/url_shortener/http/route_context.hpp`, `include/url_shortener/http/router.hpp`, `src/http/router.cpp`, `include/url_shortener/http/request_handlers.hpp`, `src/http/request_handlers.cpp`, `src/http/http_session.cpp` | modify | **Peer plumbing, delivered here** (formerly subtask 09 step 0): `RouteContext += std::optional<boost::asio::ip::address> peer;`, `Router::dispatch`/`handleShortenerRequest` gain a defaulted `peer = std::nullopt`, `http_session.cpp` reads `remote_endpoint(ec)` (`ec` -> `std::nullopt`) and passes `.address()`. Behavior-neutral (defaulted arg; redirect path ignores `peer`, C10). See subtask 09 step 0 for the exact per-file lines; this subtask performs those edits so the throttle has a peer. |
| `tests/unit/http/17_rest_create_via_registry.cpp` | create | Characterization + edge cases (written first against the old code). |
| `sources.cmake` | modify | `HTTP_UNIT_SOURCES` += the test. |

Unchanged: `CreateSlugFieldMode`; `compatibility_handlers.cpp` still calls `createLinkFromRequest` (now forwarding its `RouteContext`). The only header change is the added `const RouteContext&` parameter on `createLinkFromRequest` (above), needed for the throttle key; the REST field/error contract is otherwise untouched.

## Classes and functions

No new public symbols. **Create throttle (T-75), applied first** - before building `args` and before `execute_resource_command`. The
limiter is always present by default (dedicated in-memory, or Redis when `StorageConfig.rate_limit` selects it); the throttle is skipped
only when `link_create_rate_limit == 0` (disabled) or the request carries no peer:

```cpp
// peer: context.peer (std::optional<boost::asio::ip::address>); empty -> request not throttled (no key).
// createRateLimiter() is reached from the same process-wide LinkBackendHandle that make_request_runtime
// uses (subtask 02 / 03.0/18); it returns the selected create limiter (in-memory default, or Redis).
if (auto limiter = activeLinkBackend().createRateLimiter(); limiter && context.peer && config.link_create_rate_limit > 0) {
    RateLimitError rl_error{};
    const auto decision = limiter->Allow(context.peer->to_string(),
                                         config.link_create_rate_limit,
                                         std::chrono::seconds(config.link_create_rate_window_seconds),
                                         &rl_error);
    // over limit = !allowed AND no error; a limiter failure (error set) is fail-open -> fall through.
    if (!decision.allowed && rl_error == RateLimitError::none) {
        return makeApiErrorResponse(req, config, is_tls, 429, "rate_limited", "Too many requests");
    }
}
```

Then, after the legacy checks have produced locals (`raw_url`, optional `slug`,
`redirect_type_raw`, `expires_at_raw`, `enabled_raw`, `tags`, `metadata`, `campaign`):

```cpp
commands::args_builder args;
args.add_string("target_url", *raw_url);
if (slug)             args.add_string("slug", *slug);
if (redirect_type_raw) args.add_string("redirect_type", *redirect_type_raw);
if (expires_at_raw)    args.add_string("expires_at", *expires_at_raw);
if (enabled_raw)       args.add_bool("enabled", *enabled_raw);
if (tags)              args.add_string_list("tags", *tags);
if (metadata)          args.add_string_map("metadata", std::map<std::string,std::string>(metadata->begin(), metadata->end()));
if (campaign_present)  campaign_is_null ? args.add_null("campaign") : args.add_raw("campaign", campaign_object_text);
// campaign_object_text: a second args_builder with only the known members that are present
const auto response = execute_resource_command(config, "link.create", args.str(), request_id_of(req));
return resource_response(req, config, is_tls, response, 201);
```

`request_id_of(req)` is the existing request-id extraction used by `makeApiErrorResponse` (`requestIdFromRequest`); if it is file-local to
`request_handlers.cpp`, expose it through `request_handlers.hpp` (one declaration) rather than duplicating it.

**Peer plumbing (delivered by this subtask):** the throttle key is `context.peer`. This subtask **performs the behavior-neutral peer
plumbing itself** (`RouteContext::peer` + `remote_endpoint()` in `http_session.cpp` + `Router::dispatch`/`handleShortenerRequest`
threading; the exact per-file edits are specified in subtask 09 step 0). Subtask 09 then only **verifies** that plumbing and consumes it for
the gated endpoint. This keeps the dependency acyclic: the plumbing lands inside 04 (which precedes 06 and 09), so 09 does not need to wait
on anything new. Existing call sites compile unchanged because the new `peer` parameter is defaulted to `std::nullopt`.

## Implementation steps

1. **Characterize first:** write `17_rest_create_via_registry.cpp` and run it against the unmodified code; green.
2. Rewrite the tail of `createLinkFromRequest`; leave the ordered per-field checks byte-for-byte as they are.
3. Delete the now-unused includes/locals (`app::CreateLinkCommand`).
4. Run the new test, `10_link_handlers`, `11_compatibility_handlers`, `02_endpoint_matrix_characterization`, `03_method_and_error_characterization`.

## Commands

```powershell
cmake --build cmake-build --target 17_rest_create_via_registry 10_link_handlers 11_compatibility_handlers 02_endpoint_matrix_characterization 03_method_and_error_characterization
ctest --test-dir cmake-build -R "^(17_rest_create_via_registry|10_link_handlers|11_compatibility_handlers|02_endpoint_matrix_characterization|03_method_and_error_characterization)$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/http/17_rest_create_via_registry.cpp` | `17_rest_create_via_registry` | unit | success `201` with only `url`; with only `target_url`; with both (the `url` value wins, assert stored target); response body target key is `url`; all optional fields round-trip (tags, metadata, campaign with six keys, `redirect_type`, `expires_at`, `enabled:false`); `campaign:null`, `campaign:{}`, campaign with an unknown inner key (ignored); unknown top-level key ignored (still 201); no url -> `400 invalid_url`; each per-field error code from the list above, **and the order** (a body with bad `redirect_type` and bad `tags` reports `invalid_redirect_type`; missing url wins over everything); `deleted_at` -> `400 invalid_request`; 21 tags -> `400 invalid_request` "tags violate constraints"; 5000-character URL -> 201; reserved slug -> 409 `reserved_slug`; duplicate slug -> 409 `slug_conflict`; private target without `shortener_allow_private_targets` -> 400 `invalid_url`; compat route: `code` accepted, `slug` wins over `code`, canonical route ignores `code`; a hostile body (quotes/newlines in url) is stored escaped and returned as valid JSON; `X-Request-Id` present on success and error. **Rate limit (T-75, on by default):** with the default in-memory create limiter and `link_create_rate_limit = N` from one peer, the `(N+1)`th create -> `429 rate_limited` (fixed body), store unchanged; the throttle **key is the peer address** - an `X-Forwarded-For` header pointing at a different value does **not** change the bucket (same peer still throttled); a limiter whose `Allow` reports a failure (`RateLimitError::unavailable`) -> the create **proceeds** (`201`, fail-open); `--link-create-rate-limit 0` -> no throttle (every create 201); an empty `context.peer` -> that request not throttled; a `StorageConfig.rate_limit` with `backend == "redis"` -> `createRateLimiter()` returns the Redis limiter (asserted via a stub), while `inmemory`/disabled -> the dedicated in-memory limiter. |

Manual check: `curl -s -X POST localhost:8000/api/v1/links -d '{"url":"https://example.com/a"}'` on this branch and on `master` return the same fields (ids and timestamps differ).

## Constraints

- C3: the 400 codes and their order are the contract; the existing characterization tests are not edited.
- C1: after this change `createLinkFromRequest` has no DTO and no service call; it may validate request *shape* to choose REST error codes only.
- Q18: canonical arg name is `target_url`; the `url` alias lives only here.
- Untrusted input is escaped by `args_builder`; never concatenated.
- C10 / no other handler is touched (the throttle is on the create path only, never the redirect fast path); C++17; naming per `.clang-tidy`.
- T-75: the throttle is on by default (dedicated in-memory limiter, 30/60 s), keys on the TCP peer address only (never a header), is fail-open on limiter failure, and is disabled only by `--link-create-rate-limit 0`; `dry_run` parity is enforced on the generic endpoint (subtask 09), since the REST resource create has no `dry_run`. `rate_limited`/429 is a transport-level adapter code, not a `CommandErrorCode` (no 01.0/02 table change).

## Success criteria

1. All five targets pass without editing the pre-existing tests.
2. `grep -n "CreateLinkCommand\|makeCommandService\|CreateLink(" src/http/handlers/link_handlers.cpp` shows no use in `createLinkFromRequest`.
3. A POST whose body contains an unknown key still returns 201 (test).

## Out of scope / risks

- Fixing the text-scanning quirks of `extractJson*` (a field name inside a string value can be misread); documented, behavior-preserving.
- The compat route keeps its own request-body documentation (`url`/`code`); no doc change here.
- **Create throttle is on by default (T-75, 2026-11-16 decision):** a dedicated in-memory create limiter is constructed unconditionally at startup (30 creates / 60 s / client address), so link creation is throttled out of the box with no configuration; operators raise/lower it with `--link-create-rate-limit` / `--link-create-rate-window-seconds`, disable it with `--link-create-rate-limit 0`, or share one window across processes by enabling a Redis `StorageConfig.rate_limit`. This is an intentional additive behavior change (new 429 for abusive create bursts); it never affects the redirect fast path or the CLI.
