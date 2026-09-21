# 01 - Extend RouteDescriptor for documentation metadata

**Parent task:** 08.0 OpenAPI/docs cleanup
**State:** ✅ Complete
**Depends on:** none (depends on Task 07.0 being complete)
**Blocks:** 02

## Objective

Prepare route metadata for generated API documentation without changing
dispatch behavior.

## Files to modify

- `include/url_shortener/http/RouteRegistry.hpp`
- `src/http/RouteRegistry.cpp`
- `tests/unit/http/01_route_registry.cpp`

Implementation note: shipped as `include/url_shortener/http/route_registry.hpp`
and `src/http/route_registry.cpp` (renamed to snake_case by commit
`3f4c170 Rename files to snake_style`); the test file matches the originally
proposed name and number.

## New metadata fields

Extend `RouteDescriptor` with optional fields such as:

```cpp
std::vector<std::string> tags;
std::string operation_id;
std::vector<RouteParameterDoc> path_parameters;
std::vector<RouteParameterDoc> query_parameters;
std::vector<RouteResponseDoc> responses;
bool compatibility_alias = false;
bool placeholder = false;
```

Define small structs in the same header:

```cpp
struct RouteParameterDoc
{
    std::string name;
    std::string description;
    bool required = true;
};

struct RouteResponseDoc
{
    unsigned status;
    std::string description;
};
```

## Tests

Update route registry tests to assert:

- every route has an `operation_id`;
- every `{param}` in `path_pattern` has a matching parameter doc;
- every route has at least one response doc;
- compatibility routes are marked;
- placeholder routes are marked.

## Constraints

- No new JSON/OpenAPI dependency.
- Existing route label tests still pass.

## Success criteria

- [x] `include/url_shortener/http/route_registry.hpp` declares
      `RouteParameterDoc`, `RouteResponseDoc`, and the extended
      `RouteDescriptor` fields shown above.
- [x] `tests/unit/http/01_route_registry.cpp` asserts operation ids,
      parameter docs, response docs, and compatibility/placeholder markers
      for every route.
- [x] No new JSON/OpenAPI dependency was introduced.
- [x] Existing route label tests still pass.
