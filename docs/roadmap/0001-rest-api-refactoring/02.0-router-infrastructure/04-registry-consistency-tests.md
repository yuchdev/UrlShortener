# 04 - Add registry and router consistency checks

**Parent task:** 02.0 Router infrastructure
**State:** ✅ Complete
**Depends on:** 01, 02, 03
**Blocks:** none

## Objective

Ensure route metadata and dispatch registrations cannot drift silently.

## Files to change

- `include/url_shortener/http/Router.hpp`
- `src/http/Router.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

Implementation note: shipped as `include/url_shortener/http/router.hpp`,
`src/http/router.cpp`, and
`tests/unit/http/08_router_registry_consistency.cpp` (renamed to snake_case
by commit `3f4c170 Rename files to snake_style`; test renumbered to follow
the Stage 00/Task 01.0 characterization files).

## New API

Add read-only route inspection to `Router`:

```cpp
struct RouteEntryView
{
    std::string method;
    std::string path_pattern;
    std::string route_label;
};

std::vector<RouteEntryView> routes() const;
```

This is for tests and documentation tooling only. It must not expose handler
callables.

## Tests

`08_router_registry_consistency.cpp` must:

- build the application router through `RouterBuilder::buildApplicationRouter`;
- compare every `RouteDescriptor` from `registeredRoutes()` with a
  corresponding `RouteEntryView`;
- compare every `RouteEntryView` with a corresponding `RouteDescriptor`;
- assert route labels match;
- assert no duplicate `(method, path_pattern)` pairs exist.

## Constraints

- Any future route added to dispatch without registry metadata fails a unit
  test.
- Any registry-only route without dispatch fails a unit test, unless explicitly
  marked as documentation-only in a later metadata extension.

## Success criteria

- [x] `Router::routes()` returns `RouteEntryView` entries and exposes no
      handler callables.
- [x] `tests/unit/http/08_router_registry_consistency.cpp` cross-checks
      `registeredRoutes()` against `Router::routes()` in both directions and
      asserts no duplicate `(method, path_pattern)` pairs.
- [x] The consistency test continued to pass through every later route
      migration (Tasks 03.0-08.0), confirming registry/dispatch drift is
      caught automatically.
