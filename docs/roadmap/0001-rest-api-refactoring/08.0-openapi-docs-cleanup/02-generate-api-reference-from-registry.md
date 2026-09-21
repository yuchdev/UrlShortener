# 02 - Generate API reference from RouteRegistry

**Parent task:** 08.0 OpenAPI/docs cleanup
**State:** ✅ Complete
**Depends on:** 01
**Blocks:** 03

## Objective

Create a lightweight documentation generator that consumes `registeredRoutes()`
and produces an API reference artifact.

## Files to add

- `tools/route_registry_dump.cpp` or `tools/route_registry_dump.py`
- `docs/api/README.md` or `docs/api/openapi-stub.md`
- `tests/unit/docs/02_route_registry_docs.cpp`

Implementation note: shipped exactly at these paths -
`tools/route_registry_dump.cpp`, `docs/api/README.md`, and
`tests/unit/docs/02_route_registry_docs.cpp` - no renames.

## Implementation options

Preferred option:

- add a small C++ test/helper target that links `url_shortener_common` and
  prints route metadata as Markdown;
- keep generated docs checked in only after review.

Alternative option:

- add a runtime CLI flag such as `--list-routes`;
- implement in `src/cli_parser.cpp` and `src/main.cpp`;
- this requires more production code and should be reviewed separately.

Implementation note: the preferred option was selected -
`tools/route_registry_dump.cpp` is the small C++ helper that links the route
registry and emits Markdown, checked in at `docs/api/README.md`. The
alternative runtime `--list-routes` CLI flag was not implemented.

## Restrictions

- Do not add a JSON library.
- Do not expose `GET /api/v1/openapi.json` in this task unless a review
  decision explicitly approves runtime documentation.

## Tests

- assert all routes appear in generated output;
- assert path parameters appear in output;
- assert placeholder and compatibility markers appear in output.

## Constraints

- Route metadata is demonstrably usable for documentation.
- Generated artifact is reviewable and deterministic.

## Success criteria

- [x] `tools/route_registry_dump.cpp` exists and generates `docs/api/README.md`
      from `registeredRoutes()`.
- [x] `tests/unit/docs/02_route_registry_docs.cpp` asserts all routes, path
      parameters, and placeholder/compatibility markers appear in generated
      output.
- [x] No JSON library was added.
- [x] No runtime `GET /api/v1/openapi.json` endpoint was added - the
      generated `docs/api/README.md` artifact is the documentation output for
      this milestone.
- [x] Generated artifact (`docs/api/README.md`) is reviewable and
      deterministic - a checked-in Markdown table derived from the route
      registry.
