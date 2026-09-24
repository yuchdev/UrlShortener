# 03 - API Client and Zod Schemas

**Parent task:** 01.0 Web App Shell, Static Hosting, and Shorten Flow
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** 04

## Objective

Build the typed API client and Zod schemas the UI uses to talk to the
existing public `/api/v1/links` endpoints, including parsing of the
0004-specific error envelope. This is the single point where backend response
shapes are pinned.

## Files to add

```text
web/app/src/api/apiClient.ts
web/app/src/api/linksApi.ts
web/app/src/api/schemas.ts
web/app/src/api/errors.ts
web/app/src/model/linkView.ts
web/app/src/hooks/useCreateLink.ts
```

## Requirements

1. `apiClient.ts` is a thin fetch wrapper: base URL derived from
   `app/config.ts`, JSON content type, and a single place that inspects the
   response status and either returns parsed success JSON or throws a typed
   error built from the SC2 envelope. No auth headers, no cookies — public
   API (see [plan.md SC1](/docs/roadmap/0004-web-ui/plan.md)).
2. `schemas.ts` defines Zod schemas for the **verified** response shapes:
   - Create (`POST /api/v1/links`): the `serializeLinkViewJson` shape —
     `{ id, slug, url, short_url, status, redirect_type, ... }` (see
     `src/app/link_command_service.cpp`). Model the fields the UI actually
     uses (`slug`, `url`, `short_url`, `status`, `redirect_type`) as required;
     allow unknown extra fields.
   - Preview (`GET /api/v1/links/{slug}/preview`):
     `{ slug, url, status, redirect_type, enabled, expires_at, deleted_at }`
     (consumed by task 03.0, but define the schema here alongside the others).
   - Error envelope (SC2): `{ error: { code, message, request_id } }` — note
     `request_id` is **not** `req_`-prefixed; do not assume 0002's C4 format.
3. `errors.ts` defines a typed error class/type carrying `code`, `message`,
   `request_id`, and the HTTP status, produced whenever the response is not
   a success. The exhaustive mapping of codes/statuses to UI states is task
   02.0; this subtask only needs to *parse and carry* them faithfully.
4. `model/linkView.ts` exports the TypeScript types inferred from the Zod
   schemas (`z.infer`), so the rest of the app depends on inferred types, not
   hand-written duplicates.
5. `linksApi.ts` exposes `createLink(input)` (and a `previewLink(slug)` stub
   or full impl for task 03.0) that call `apiClient` and validate the
   response with the Zod schemas, throwing the typed error on envelope
   responses or schema-validation failure.
6. `useCreateLink.ts` wraps `createLink` in a TanStack Query mutation.

## Constraints

- Response shapes are treated as the contract; if the backend changes, these
  schemas are the only place to update.
- Do not invent fields the backend does not return. If a field the UI wants
  isn't in the verified shapes, surface that gap to `app-architect` rather
  than fabricating it.
- No fault-to-UI-state mapping here — that is task 02.0. This subtask parses
  faithfully and stops.

## Success criteria

- [ ] `createLink` posts to `/api/v1/links` and returns a Zod-validated,
      typed link view on success.
- [ ] A non-2xx response is parsed into the typed error carrying `code`,
      `message`, `request_id`, and status.
- [ ] Types consumed by the UI are inferred from the Zod schemas, not
      duplicated by hand.
- [ ] The client sends no auth headers or cookies.
