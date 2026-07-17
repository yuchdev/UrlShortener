# Task 01.02 - Implement path-pattern matching

## Objective

Add a dependency-free router matcher that can match route patterns and extract
path parameters.

## Files to add

- `include/url_shortener/http/Router.hpp`
- `src/http/Router.cpp`
- `tests/unit/http/03_router_matcher.cpp`

## Files to modify

- `sources.cmake` - add `src/http/Router.cpp` and `Router.hpp`.
- `CMakeLists.txt` - add test to `HTTP_UNIT_SOURCES`.

## New class

`url_shortener::http::Router`

Required public methods:

```cpp
class Router
{
public:
    static bool matchPath(
        const std::string& path_pattern,
        const std::string& target,
        RouteContext* context);
};
```

This task only adds `matchPath`; dispatch is added later.

## Matching rules

- Strip query string from `target` and copy it into
  `RouteContext::query_string`.
- Split path and pattern by `/`.
- Ignore empty leading segment from the initial slash.
- Require equal segment counts.
- Literal segments are case-sensitive.
- Capture segments use `{name}`.
- Captured segment values must be non-empty.
- Do not URL-decode captured values.
- Do not validate slug syntax in the matcher.

## Tests

In `03_router_matcher.cpp`, cover:

- exact match `/healthz`;
- parameter match `/api/v1/links/{slug}`;
- two literal plus one capture;
- query stripping for stats route;
- segment count mismatch;
- literal mismatch;
- empty capture rejection;
- root path `/`;
- `/{slug}` does not match `/api/v1/links`.

## Acceptance criteria

- `matchPath` is deterministic and has no global state.
- Production dispatch remains unchanged.

