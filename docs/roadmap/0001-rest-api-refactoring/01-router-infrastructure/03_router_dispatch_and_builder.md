# Task 01.03 - Add Router dispatch and RouterBuilder

## Objective

Add route registration and dispatch mechanics using stub handlers first. Do not
route production requests yet.

## Files to add

- `include/url_shortener/http/RouterBuilder.hpp`
- `src/http/RouterBuilder.cpp`
- `tests/unit/http/04_router_dispatch.cpp`

## Files to modify

- `include/url_shortener/http/Router.hpp`
- `src/http/Router.cpp`
- `sources.cmake`
- `CMakeLists.txt`

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

In `04_router_dispatch.cpp`, cover:

- first-match-wins ordering;
- `/api/v1/links/id/{id}` before `/api/v1/links/{slug}`;
- `/r/{slug}` before `/{slug}`;
- method mismatch;
- no match;
- handler receives `RouteContext::path_params`;
- handler receives `RouteContext::query_string`.

## Acceptance criteria

- `Router` has no link repository, cache, analytics, or TLS dependency.
- `RouterBuilder` is the only file that knows registration order.
- `handleShortenerRequest()` still uses the old branch chain.

