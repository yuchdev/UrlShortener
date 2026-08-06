# Task 07.02 - Generate API reference from RouteRegistry

## Objective

Create a lightweight documentation generator that consumes `registeredRoutes()`
and produces an API reference artifact.

## Files to add

- `tools/route_registry_dump.cpp` or `tools/route_registry_dump.py`
- `docs/api/README.md` or `docs/api/openapi-stub.md`
- `tests/unit/docs/02_route_registry_docs.cpp`

## Implementation options

Preferred option:

- add a small C++ test/helper target that links `url_shortener_common` and
  prints route metadata as Markdown;
- keep generated docs checked in only after review.

Alternative option:

- add a runtime CLI flag such as `--list-routes`;
- implement in `src/cli_parser.cpp` and `src/main.cpp`;
- this requires more production code and should be reviewed separately.

## Restrictions

- Do not add a JSON library.
- Do not expose `GET /api/v1/openapi.json` in this task unless a review
  decision explicitly approves runtime documentation.

## Tests

- assert all routes appear in generated output;
- assert path parameters appear in output;
- assert placeholder and compatibility markers appear in output.

## Acceptance criteria

- Route metadata is demonstrably usable for documentation.
- Generated artifact is reviewable and deterministic.

