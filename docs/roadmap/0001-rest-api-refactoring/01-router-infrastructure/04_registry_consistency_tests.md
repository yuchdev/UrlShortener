# Task 01.04 - Add registry and router consistency checks

## Objective

Ensure route metadata and dispatch registrations cannot drift silently.

## Files to change

- `include/url_shortener/http/Router.hpp`
- `src/http/Router.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

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

`05_router_registry_consistency.cpp` must:

- build the application router through `RouterBuilder::buildApplicationRouter`;
- compare every `RouteDescriptor` from `registeredRoutes()` with a
  corresponding `RouteEntryView`;
- compare every `RouteEntryView` with a corresponding `RouteDescriptor`;
- assert route labels match;
- assert no duplicate `(method, path_pattern)` pairs exist.

## Acceptance criteria

- Any future route added to dispatch without registry metadata fails a unit
  test.
- Any registry-only route without dispatch fails a unit test, unless explicitly
  marked as documentation-only in a later metadata extension.

