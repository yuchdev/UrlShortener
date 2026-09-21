# 01 - Add route context and handler type aliases

**Parent task:** 02.0 Router infrastructure
**State:** ✅ Complete
**Depends on:** none
**Blocks:** 02, 03, 04

## Objective

Introduce the small shared types that route handlers will use, without changing
production dispatch.

## Files to add

- `include/url_shortener/http/RouteContext.hpp`
- `include/url_shortener/http/HandlerTypes.hpp`

Implementation note: shipped as
`include/url_shortener/http/route_context.hpp` and
`include/url_shortener/http/handler_types.hpp` (renamed to snake_case by
commit `3f4c170 Rename files to snake_style`).

## Files to modify

- `sources.cmake` - add new headers to `URL_SHORTENER_H`.

## New API

In `route_context.hpp`:

```cpp
namespace url_shortener::http {

using PathParams = std::unordered_map<std::string, std::string>;

struct RouteContext
{
    PathParams path_params;
    std::string query_string;
    std::string route_label;
};

std::optional<std::string> pathParam(
    const RouteContext& context,
    const std::string& name);

} // namespace url_shortener::http
```

In `handler_types.hpp`:

```cpp
namespace url_shortener::http {

using BeastRequest =
    boost::beast::http::request<boost::beast::http::string_body>;

using BeastResponse =
    boost::beast::http::response<boost::beast::http::string_body>;

using HandlerFn = std::function<BeastResponse(
    const BeastRequest& req,
    const ServerConfig& config,
    bool is_tls,
    const RouteContext& context)>;

} // namespace url_shortener::http
```

## Tests

Add `tests/unit/http/02_route_context.cpp` and register it in
`HTTP_UNIT_SOURCES`.

Implementation note: shipped as `tests/unit/http/05_route_context.cpp` -
renumbered because the Stage 00/Task 01.0 characterization files already
occupy `02`-`04` in the final numbering.

Test:

- empty `RouteContext`;
- successful `pathParam(context, "slug")`;
- missing parameter returns `std::nullopt`;
- `query_string` is stored unchanged.

## Constraints

- No changes to `handleShortenerRequest()`.
- No new external dependencies.
- New public symbols have Doxygen comments.

## Success criteria

- [x] `include/url_shortener/http/route_context.hpp` and
      `include/url_shortener/http/handler_types.hpp` exist with the API
      shown above.
- [x] `tests/unit/http/05_route_context.cpp` exists and covers all four
      required cases.
- [x] No changes to `handleShortenerRequest()` in this task.
- [x] No new external dependencies.
- [x] New public symbols carry Doxygen comments.
