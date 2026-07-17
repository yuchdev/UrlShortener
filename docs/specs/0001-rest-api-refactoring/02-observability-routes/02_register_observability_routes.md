# Task 02.02 - Register observability handlers in RouterBuilder

## Objective

Bind real observability handlers into `RouterBuilder`.

## Files to modify

- `src/http/RouterBuilder.cpp`
- `tests/unit/http/04_router_dispatch.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

## Route entries

Register in this order:

```text
GET /healthz -> handleHealthz, label healthz
GET /readyz  -> handleReadyz, label readyz
GET /metrics -> handleMetrics, label metrics
```

Use `boost::beast::http::verb::get`.

## Tests

Add dispatch tests that build the application router and issue requests to:

- `GET /healthz`
- `GET /readyz`
- `GET /metrics`

Assert the real handler output.

## Acceptance criteria

- Registry/router consistency tests pass.
- No production call site uses the router yet.

