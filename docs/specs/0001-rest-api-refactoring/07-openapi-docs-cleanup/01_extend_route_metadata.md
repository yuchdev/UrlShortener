# Task 07.01 - Extend RouteDescriptor for documentation metadata

## Objective

Prepare route metadata for generated API documentation without changing
dispatch behavior.

## Files to modify

- `include/url_shortener/http/RouteRegistry.hpp`
- `src/http/RouteRegistry.cpp`
- `tests/unit/http/01_route_registry.cpp`

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

## Acceptance criteria

- No new JSON/OpenAPI dependency.
- Existing route label tests still pass.

