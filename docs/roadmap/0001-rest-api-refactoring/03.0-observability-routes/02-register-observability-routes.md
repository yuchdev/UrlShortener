# 02 - Register observability handlers in RouterBuilder

**Parent task:** 03.0 Observability routes
**State:** ✅ Complete
**Depends on:** 01
**Blocks:** 03

## Objective

Bind real observability handlers into `RouterBuilder`.

## Files to modify

- `src/http/RouterBuilder.cpp`
- `tests/unit/http/04_router_dispatch.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

Implementation note: shipped as `src/http/router_builder.cpp`,
`tests/unit/http/07_router_dispatch.cpp`, and
`tests/unit/http/08_router_registry_consistency.cpp` (renamed to snake_case
by commit `3f4c170 Rename files to snake_style`; tests renumbered to follow
the router-infrastructure test files, see Task 02.0).

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

## Constraints

- Registry/router consistency tests pass.
- No production call site uses the router yet.

## Success criteria

- [x] `src/http/router_builder.cpp` registers `GET /healthz`, `GET /readyz`,
      and `GET /metrics` in that order, bound to the real handlers.
- [x] `tests/unit/http/07_router_dispatch.cpp` issues requests to all three
      routes and asserts real handler output.
- [x] `tests/unit/http/08_router_registry_consistency.cpp` passes with the
      three observability routes registered.
- [x] No production call site used the router yet at the end of this
      subtask (switched in subtask 03).
