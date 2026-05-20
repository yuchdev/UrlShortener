# Task 09 — Tests, CI, and Documentation

## Goal

Add complete test coverage, CI wiring, and documentation for the React admin console, admin API, fingerprint provider layer, and analytics pipeline.

## Required documentation files

Create:

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

## Required frontend test setup

Create:

```text
web/admin/vitest.config.ts
web/admin/playwright.config.ts
web/admin/src/test/setupTests.ts
web/admin/src/test/msw/handlers.ts
web/admin/src/test/msw/server.ts
web/admin/e2e/login.spec.ts
web/admin/e2e/dashboard.spec.ts
web/admin/e2e/permissions.spec.ts
web/admin/e2e/url-pairs.spec.ts
```

## Required backend test groups

Ensure these exist:

```text
tests/unit/admin/
tests/integration/admin/
tests/unit/fingerprint/
tests/integration/tracking/
tests/fixtures/fingerprints/
```

## CI requirements

Add CI jobs for:

```text
frontend lint
frontend typecheck
frontend unit tests
frontend build
frontend e2e smoke tests
backend build
backend unit tests
backend integration tests
```

If GitHub Actions is used, create or extend:

```text
.github/workflows/admin-console.yml
```

## Frontend package scripts

`web/admin/package.json` must include:

```json
{
  "scripts": {
    "dev": "vite",
    "build": "tsc -b && vite build",
    "preview": "vite preview",
    "lint": "eslint .",
    "typecheck": "tsc -b --noEmit",
    "test": "vitest run",
    "test:watch": "vitest",
    "e2e": "playwright test"
  }
}
```

## Documentation requirements

### README

Must explain:

```text
what the admin console is
how to run frontend dev server
how to build static assets
how to configure backend to serve assets
how to create first admin user
how to log in
```

### Fingerprinting docs

Must explain:

```text
production provider
fallback provider
tracking modes
privacy risks
retention settings
risk scoring
```

### Security docs

Must explain:

```text
safe login flow
session cookie model
CSRF model
roles and permissions
IP masking
raw fingerprint restrictions
audit log
export policy
```

## Exit criteria

- CI runs frontend and backend tests.
- React app builds in CI.
- Admin API integration tests run in CI.
- E2E smoke test covers login and dashboard.
- Documentation explains local setup and first-admin creation.
- Documentation explains fingerprint provider configuration.
- Documentation explains privacy and export policy.
