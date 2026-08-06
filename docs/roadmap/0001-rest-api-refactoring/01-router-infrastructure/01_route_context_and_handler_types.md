# Task 01.01 - Add route context and handler type aliases

## Objective

Introduce the small shared types that route handlers will use, without changing
production dispatch.

## Files to add

- `include/url_shortener/http/RouteContext.hpp`
- `include/url_shortener/http/HandlerTypes.hpp`

## Files to modify

- `sources.cmake` - add new headers to `URL_SHORTENER_H`.

## New API

In `RouteContext.hpp`:

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

In `HandlerTypes.hpp`:

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

Test:

- empty `RouteContext`;
- successful `pathParam(context, "slug")`;
- missing parameter returns `std::nullopt`;
- `query_string` is stored unchanged.

## Acceptance criteria

- No changes to `handleShortenerRequest()`.
- No new external dependencies.
- New public symbols have Doxygen comments.

