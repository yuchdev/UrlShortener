# Security and Privacy Test Plan

## Scope

Security tests verify authentication, authorization, untrusted input handling,
secret redaction, and privacy invariants.

## Required coverage

- Password hashing and token generation.
- Auth session creation, introspection, and revocation.
- Access guard role enforcement.
- First-admin bootstrap behavior.
- URL validation and SSRF/private-target rejection.
- Slug validation and reserved slug rejection.
- Request body and target-size hardening.
- Log redaction for secrets, DSNs, tokens, keys, and salts.

## Acceptance criteria

- Failed auth does not reveal whether username or password was wrong.
- Raw tokens, passwords, hashes, client secrets, TLS private keys, DSNs, and
  analytics salts are never logged or returned.
- CLI and REST share the same target URL and slug rejection behavior.

## E2E analogs

- `tests/e2e/scripts/sections/05_fingerprinting.sh`
- `tests/e2e/scripts/sections/06_authentication.sh`
- `tests/e2e/scripts/sections/07_permissions.sh`
- `tests/e2e/scripts/sections/08_admin_api.sh`
- `tests/e2e/scripts/sections/09_error_handling.sh`
- `tests/e2e/scripts/sections/11_cli_link_create.sh`
