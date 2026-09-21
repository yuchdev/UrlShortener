# 02 - Implement path-pattern matching

**Parent task:** 02.0 Router infrastructure
**State:** ✅ Complete
**Depends on:** 01
**Blocks:** 03, 04

## Objective

Add a dependency-free router matcher that can match route patterns and extract
path parameters.

## Files to add

- `include/url_shortener/http/Router.hpp`
- `src/http/Router.cpp`
- `tests/unit/http/03_router_matcher.cpp`

Implementation note: shipped as `include/url_shortener/http/router.hpp`,
`src/http/router.cpp`, and `tests/unit/http/06_router_matcher.cpp` (renamed
to snake_case by commit `3f4c170 Rename files to snake_style`; test
renumbered to follow the Stage 00/Task 01.0 characterization files).

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

In `06_router_matcher.cpp`, cover:

- exact match `/healthz`;
- parameter match `/api/v1/links/{slug}`;
- two literal plus one capture;
- query stripping for stats route;
- segment count mismatch;
- literal mismatch;
- empty capture rejection;
- root path `/`;
- `/{slug}` does not match `/api/v1/links`.

## Constraints

- `matchPath` is deterministic and has no global state.
- Production dispatch remains unchanged.

## Success criteria

- [x] `include/url_shortener/http/router.hpp` declares `Router::matchPath`
      with the signature shown above.
- [x] `tests/unit/http/06_router_matcher.cpp` covers all nine listed cases.
- [x] `matchPath` is deterministic and has no global state.
- [x] Production dispatch remains unchanged by this task.
