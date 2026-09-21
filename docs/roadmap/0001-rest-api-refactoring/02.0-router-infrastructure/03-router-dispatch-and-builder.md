# 03 - Add Router dispatch and RouterBuilder

**Parent task:** 02.0 Router infrastructure
**State:** ✅ Complete
**Depends on:** 01, 02
**Blocks:** 04

## Objective

Add route registration and dispatch mechanics using stub handlers first. Do not
route production requests yet.

## Files to add

- `include/url_shortener/http/RouterBuilder.hpp`
- `src/http/RouterBuilder.cpp`
- `tests/unit/http/04_router_dispatch.cpp`

Implementation note: shipped as
`include/url_shortener/http/router_builder.hpp`,
`src/http/router_builder.cpp`, and `tests/unit/http/07_router_dispatch.cpp`
(renamed to snake_case by commit `3f4c170 Rename files to snake_style`; test
renumbered to follow the Stage 00/Task 01.0 characterization files).

## Files to modify

- `include/url_shortener/http/Router.hpp`
- `src/http/Router.cpp`
- `sources.cmake`
- `CMakeLists.txt`

Implementation note: shipped as `include/url_shortener/http/router.hpp` and
`src/http/router.cpp`.

## New Router API

```cpp
class Router
{
public:
    void add(
        boost::beast::http::verb method,
        const std::string& path_pattern,
        const std::string& route_label,
        HandlerFn handler);

    BeastResponse dispatch(
        const BeastRequest& req,
        const ServerConfig& config,
        bool is_tls) const;
};
```

## New RouterBuilder API

```cpp
class RouterBuilder
{
public:
    static Router buildApplicationRouter();
};
```

At this stage, `buildApplicationRouter()` may register stub handlers that return
distinct test responses. Real handler binding comes in later stages.

## Dispatch rules

- First matching entry wins.
- Method must match exactly.
- Route entries are stored in registration order.
- If a path matches but method does not, preserve current behavior plan by
  returning `400 invalid_method` initially.
- If no route matches, return a controlled fallback response in the same shape
  as current behavior for the route group being tested.

## Tests

In `07_router_dispatch.cpp`, cover:

- first-match-wins ordering;
- `/api/v1/links/id/{id}` before `/api/v1/links/{slug}`;
- `/r/{slug}` before `/{slug}`;
- method mismatch;
- no match;
- handler receives `RouteContext::path_params`;
- handler receives `RouteContext::query_string`.

## Constraints

- `Router` has no link repository, cache, analytics, or TLS dependency.
- `RouterBuilder` is the only file that knows registration order.
- `handleShortenerRequest()` still uses the old branch chain.

## Success criteria

- [x] `include/url_shortener/http/router.hpp` declares `Router::add` and
      `Router::dispatch` with the signatures shown above.
- [x] `include/url_shortener/http/router_builder.hpp` declares
      `RouterBuilder::buildApplicationRouter`.
- [x] `tests/unit/http/07_router_dispatch.cpp` covers all seven listed cases.
- [x] `Router` has no link repository, cache, analytics, or TLS dependency.
- [x] `RouterBuilder` is the only file that knows registration order.
- [x] `handleShortenerRequest()` still used the old branch chain at the end
      of this task (it was switched incrementally starting in Task 03.0 and
      fully cleaned up in Task 08.0).
