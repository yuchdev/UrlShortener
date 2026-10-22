# 09 - REST resource-route shim

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 08; Task 04.0 (resource routes via registry)
**Blocks:** 15, 33
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Verify that the nine existing link resource routes (plus `GET /api/v1/links`,
`GET /api/v1/commands`, `GET/POST /api/v1/jobs*`) return the **same `result`**
as the command envelope and keep today's status codes (plan.md C3), by
replaying the steps that carry a `rest_resource` block.

## Dependencies

- Subtask 08 (`ServerProcess`, `RestClient`, role strategy).
- Two caller regimes (catalog section 2 "REST access control"): the pre-existing `/api/v1/links/{slug}...` routes and
  `POST /api/v1/links` always run as `Admin` (C3), whatever role flag the server has; the new `GET /api/v1/links` and
  `/api/v1/jobs*` rows are gated and use the `--command-api-role` caller like the generic endpoint. The shim therefore starts
  the server with `--command-api-enabled true` and expects `not_authorized` from gated rows only when the case's caller is `user`.
- Catalog section 5 (static route -> command table) and existing characterization tests `tests/unit/http/02_endpoint_matrix_characterization.cpp`, `03_method_and_error_characterization.cpp` (unchanged and still required green).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/py/14_rest_resource_corpus.py` | create | `RestResourceAdapter` + run |
| `tests/conformance/py/rest_resource_status.json` | create | Documented legacy status/body-code table per command (see below) |
| `sources.cmake` | modify | Append runner |

## Classes and functions

```python
class RestResourceAdapter(Adapter):
    transport = "rest_resource"
    def begin_case(self, case, ctx): ...      # same server lifecycle as RestGenericAdapter
    def send(self, step, request_text, ctx) -> dict:
        rr = step.get("rest_resource")
        if rr is None: return self._generic.send(step, request_text, ctx)   # setup steps without a resource form
        status, body, raw = self._client.call(role(step), rr["method"], rr["path"], rr.get("query"), rr.get("body"))
        assert status == rr["status"]
        return wrap(body, status, step)       # build an envelope for matching, see below
```

**Wrapping rule (the only adapter that synthesizes an envelope):** for 2xx,
`{"v":1,"ok":true,"result": <body>}`; for 4xx/5xx,
`{"v":1,"ok":false,"error":{"code": <code from body>}}` where the code is read
with the same field the legacy handlers emit. Task 03.0's README records that legacy REST error bodies use the codes `invalid_request`, `repository`, `internal_error` (and `codeForAppError` names for the rest), not the envelope vocabulary; `rest_resource_status.json` therefore carries a `legacy_code -> envelope_code` column (the same mapping Task 01.0's error table keeps in its `legacy_*` columns) and the wrapper translates through it. **UNVERIFIED:** the JSON key that carries the code in the body (inspect `src/http/handlers/link_handlers.cpp` and `tests/unit/http/10_link_handlers.cpp`). The wrapper then lets the standard
matcher compare `result` bodies and error codes; `id` is not compared
(`caps.id = False`).

`rest_resource_status.json`: `{ "link.create": {"success": 201, "errors": {"invalid_url": 400, ...}}, ... }` - copied from the characterization tests, **not** from the new catalog, because C3 says legacy statuses win; the shim asserts `step.rest_resource.status` equals this table (so a corpus author cannot silently weaken a status).

Required `rest_resource` coverage (checked by subtask 15): at least one
passing and one failing step for each of: `link.create`, `link.get` (slug and
id), `link.update`, `link.delete`, `link.enable`, `link.disable`,
`link.restore`, `link.preview`, `link.stats`, `link.list`, `command.list`,
`job.list`, `job.get`, `job.cancel`.

## Implementation steps

1. Build `rest_resource_status.json` from the characterization tests; cross-check against catalog section 5 routes.
2. Implement adapter; reuse `RestClient.call` (adds generic method/path/query).
3. Ensure per-field 400 codes of legacy routes (e.g. `invalid_url` vs field-level codes) are asserted through the wrapped error code, with the case documenting any intentional divergence in `doc`.
4. Check `Allow` header on 405 for wrong methods in a separate small test (route table sanity) in this file.

## Commands

```bash
ctest --test-dir cmake-build -R "^conformance_14_rest_resource_corpus$" --output-on-failure
ctest --test-dir cmake-build -R "http__|app__" -L unit --output-on-failure   # characterization tests stay green
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/py/14_rest_resource_corpus.py` | `conformance_14_rest_resource_corpus` | `integration;conformance` | every step with `rest_resource`; legacy status table equality; 405 + `Allow`; trailing-slash and URL-encoded slug behaviors unchanged |

## Constraints

- Legacy bodies are never reformatted to fit the envelope; the wrapper is test-side only.
- Endpoints outside the registry (redirects, `/healthz`, compatibility aliases, URI-store fallback) are not exercised here (plan out-of-scope).
- By-`id` preview is generic-only (catalog 3.1); do not add a resource-route case for it.

## Success criteria

1. For each command above the resource result equals the generic result on the shared corpus cases.
2. Status table equals the characterization tests' expectations (no drift).
3. The error-code key assumption is verified and the UNVERIFIED note removed in the PR.

## Out of scope / risks

- Legacy `/api/v1/short-urls` aliases (compatibility handlers) keep their own characterization tests.
- Risk: legacy handlers map several internal failures to one 400 body code; where `error.code` cannot be recovered the case sets `expect` with `error.code` of the legacy vocabulary and lists a reason in `doc`.
