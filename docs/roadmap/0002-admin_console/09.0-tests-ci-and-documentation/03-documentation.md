# 03 - Documentation

**Parent task:** 09.0 Tests, CI, and Documentation
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** none

## Objective

Write the `docs/admin-console/` documentation set covering setup,
architecture, API, fingerprinting, and security/privacy - the durable
reference for operators and future contributors, distinct from this
milestone's own roadmap docs.

## Files to add

```text
docs/admin-console/README.md
docs/admin-console/react-architecture.md
docs/admin-console/admin-api.md
docs/admin-console/fingerprinting.md
docs/admin-console/security-and-privacy.md
docs/admin-console/local-development.md
docs/admin-console/testing.md
docs/admin-console/screens.md
```

## Requirements

### README.md

Must explain: what the admin console is, how to run the frontend dev
server, how to build static assets, how to configure the backend to serve
assets, how to create the first admin user, how to log in.

### react-architecture.md

Summarizes the frontend architecture from
[02-react-frontend-architecture.md](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md)
as implemented - directory layout, routing, API client, React Query
conventions, tables, charts - linking back to the source spec rather than
duplicating it wholesale.

### admin-api.md

Generated or hand-written reference for every `/admin/api/v1/*` and
`/api/v1/tracking/*` endpoint actually implemented, cross-referenced against
[04-admin-api-contract.md](/docs/roadmap/0002-admin_console/04-admin-api-contract.md).

### fingerprinting.md

Must explain: production provider (Fingerprint Pro), fallback provider
(ThumbmarkJS/FingerprintJS OSS), tracking modes (fast redirect / enhanced
analytics / beacon), privacy risks, retention settings, risk scoring - see
[03-fingerprint-and-suspicion-model.md](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md).

### security-and-privacy.md

Must explain: safe login flow, session cookie model, CSRF model, roles and
permissions, IP masking, raw fingerprint restrictions, audit log, export
policy - see
[06-security-privacy-permissions.md](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md).

### local-development.md

Step-by-step local dev setup: running `web/admin` and `web/public-tracker`
dev servers, pointing them at a local backend, seeding fixture data, running
the fallback fingerprint provider locally.

### testing.md

Summarizes the test strategy from task 09.0 subtasks 01-02: Vitest/RTL/MSW/
Playwright on the frontend, CTest unit/integration groups on the backend,
where fixtures live, how to run each suite locally and in CI.

### screens.md

A short annotated tour of each admin page (dashboard, analytics pages, URL
pairs, visitor/fingerprint/IP explorer, console users, audit log, settings),
referencing the routes from
[02-react-frontend-architecture.md §2](/docs/roadmap/0002-admin_console/02-react-frontend-architecture.md).

## Constraints

- Keep `docs/admin-console/` focused on "how to run/use/extend the shipped
  console" - design rationale and decisions stay in
  `docs/roadmap/0002-admin_console/00-decision-record.md` and friends; link
  to them instead of duplicating.
- Update `docs/roadmap/0002-admin_console/status.md`'s "Per-task detail"
  section once each task actually ships, rather than only at the very end.

## Success criteria

- [ ] `docs/admin-console/README.md` covers local setup and first-admin
      creation end to end.
- [ ] `docs/admin-console/fingerprinting.md` covers provider configuration,
      tracking modes, and risk scoring.
- [ ] `docs/admin-console/security-and-privacy.md` covers privacy and
      export policy completely enough that a new admin operator does not
      need to read the roadmap specs to understand the rules.
- [ ] Every file in the list above exists and cross-references the correct
      supporting roadmap spec doc(s).
