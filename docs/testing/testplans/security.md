# Security and Privacy Test Plan

## Scope

Security tests verify authentication, authorization, untrusted input handling,
secret redaction, privacy invariants, and the no-listener/URI-store guarantees
for the local transport modes.

## Required coverage

- Password hashing and token generation.
- Auth session creation, introspection, and revocation.
- Access guard role enforcement.
- First-admin bootstrap behavior.
- URL validation and SSRF/private-host rejection (numeric IPv4 dec/hex/octal,
  bracketed IPv6, embedded private ranges, `%`-in-host, underscore hosts).
- Slug validation and reserved slug rejection.
- Request body and target-size hardening.
- Log redaction for secrets, DSNs, tokens, keys, and salts.
- Command table allowlist and browse redaction (`commands__70`, `commands__71`).
- `system.info` key set guard (`commands__72`): only allow-listed names emitted.
- Authorization descriptor matrix (`commands__74`): every mutating command
  requires at least write base permission.
- Authorization before side effects (`commands__75`): permission check runs
  before any storage write.
- Import SSRF, oversized payload, and path confinement (`commands__76`,
  `commands__77`, `commands__78`).
- No HTTP listener and no `uri.txt` creation in `cmd`, `serve-stdio`, and `tui`
  modes (`commands__79`).
- Redirect path isolation from the command registry (`commands__80`).
- TUI no-secrets screen guard (`tui__34`): rendered frames contain no secret
  markers even with the real registry.

## Acceptance criteria

- Failed auth does not reveal whether username or password was wrong.
- Raw tokens, passwords, hashes, client secrets, TLS private keys, DSNs, and
  analytics salts are never logged or returned.
- CLI and REST share the same target URL and slug rejection behavior.
- Private-target SSRF vectors (`http://2130706433/`, `http://0x7f000001/`,
  `http://0177.0.0.1/`, `http://[::1]/`, IPv6-embedded, NAT64/6to4, CGNAT,
  broadcast) all return `invalid_url` when private targets are disallowed.
- A `%` in the host is always refused; underscore hosts are refused.
- `cmd`, `serve-stdio`, and non-TTY `tui` never bind a TCP or UNIX-domain
  listening socket and never write `uri.txt`.

## E2E analogs

- `tests/e2e/scripts/sections/05_fingerprinting.sh`
- `tests/e2e/scripts/sections/06_authentication.sh`
- `tests/e2e/scripts/sections/07_permissions.sh`
- `tests/e2e/scripts/sections/08_admin_api.sh`
- `tests/e2e/scripts/sections/09_error_handling.sh`
- `tests/e2e/scripts/sections/11_cli_link_create.sh`
- `tests/e2e/scripts/sections/19_cmd_serve_stdio_no_listener.sh`

## Conformance analogs

- `conformance_30_authz_process_level` — process-level authz denial before side
  effects; flag cannot be overridden by args.
- `conformance_31_import_limits_process_level` — import SSRF, size cap, and
  path policy at process level.
- `conformance_32_no_listener_process_level` — `cmd` and `serve-stdio` own no
  listening socket.
- `conformance_authz_matrix_check` — drift check between corpus
  `42_authorization_matrix.json` and the live registry descriptor matrix.
